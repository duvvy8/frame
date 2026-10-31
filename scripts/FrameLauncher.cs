using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Microsoft.Win32;

[assembly: AssemblyTitle("Frame")]
[assembly: AssemblyProduct("Frame")]
[assembly: AssemblyDescription("Launch the current installed Frame release")]
[assembly: AssemblyVersion("1.0.0.0")]

public sealed class FrameLaunchPlan
{
    public string Executable;
    public string WorkingDirectory;
    public string[] Arguments;
}

public static class FrameLauncher
{
    private static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength = 32 * 1024 * 1024 };

    [STAThread]
    public static int Main(string[] args)
    {
        try
        {
            FrameLaunchPlan plan = BuildPlan(FindRoot(), args);
            ProcessStartInfo start = new ProcessStartInfo(plan.Executable, JoinArguments(plan.Arguments));
            start.WorkingDirectory = plan.WorkingDirectory;
            start.UseShellExecute = false;
            Process.Start(start);
            return 0;
        }
        catch (Exception error)
        {
            MessageBox.Show("Frame could not open the current installed release.\n\n" + error.Message +
                "\n\nNo older version was started.", "Frame", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }

    private static string FindRoot()
    {
        using (RegistryKey key = Registry.CurrentUser.OpenSubKey(@"Software\Frame\Launcher"))
        {
            string registered = key == null ? null : key.GetValue("InstallRoot") as string;
            if (!String.IsNullOrEmpty(registered) && File.Exists(Path.Combine(registered, "scripts", "frame-launch.json")))
                return Path.GetFullPath(registered);
        }
        DirectoryInfo current = new FileInfo(Assembly.GetExecutingAssembly().Location).Directory;
        while (current != null)
        {
            if (File.Exists(Path.Combine(current.FullName, "scripts", "frame-launch.json"))) return current.FullName;
            current = current.Parent;
        }
        throw new InvalidOperationException("The Frame installation could not be found. Run its launch repair script.");
    }

    private static string InsideRoot(string root, string relative)
    {
        if (Path.IsPathRooted(relative)) throw new InvalidOperationException("The release configuration contains an absolute path.");
        string result = Path.GetFullPath(Path.Combine(root, relative));
        if (!result.StartsWith(root.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("The release configuration points outside Frame's installation.");
        return result;
    }

    private static Dictionary<string, object> ReadJson(string file)
    {
        return Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(file, Encoding.UTF8));
    }

    public static FrameLaunchPlan BuildPlan(string root, string[] args)
    {
        root = Path.GetFullPath(root);
        Dictionary<string, object> config = ReadJson(Path.Combine(root, "scripts", "frame-launch.json"));
        string release = InsideRoot(root, (string)config["releaseDirectory"]);
        string executable = Path.Combine(release, "chrome.exe");
        string manifestFile = Path.Combine(release, "Frame.runtime.json");
        if (!File.Exists(executable) || !File.Exists(manifestFile))
            throw new FileNotFoundException("The current release is missing or its deployment is incomplete.");
        Dictionary<string, object> manifest = ReadJson(manifestFile);
        string expectedHash = null;
        Dictionary<string, long> expectedSizes = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
        foreach (object item in (IEnumerable)manifest["files"])
        {
            Dictionary<string, object> record = (Dictionary<string, object>)item;
            string name = (string)record["path"];
            expectedSizes[name] = Convert.ToInt64(record["bytes"]);
            if (name == "chrome.exe") expectedHash = (string)record["sha256"];
        }
        foreach (string name in new[] { "chrome.exe", "chrome.dll", "chrome_elf.dll", "resources.pak", "icudtl.dat", "v8_context_snapshot.bin" })
        {
            string required = Path.Combine(release, name);
            if (!expectedSizes.ContainsKey(name) || !File.Exists(required) || new FileInfo(required).Length != expectedSizes[name])
                throw new InvalidOperationException("The current release is incomplete: " + name);
        }
        string actualHash;
        using (SHA256 hash = SHA256.Create())
        using (FileStream stream = File.OpenRead(executable))
            actualHash = BitConverter.ToString(hash.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
        if (!String.Equals(actualHash, expectedHash, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("The current browser executable does not match its release inventory.");

        List<string> forwarded = new List<string>();
        bool explicitProfile = false;
        bool literalTail = false;
        foreach (string arg in args)
        {
            // Everything following --single-argument is a URL/file, even if it
            // contains text resembling a switch.
            if (literalTail) { forwarded.Add(arg); continue; }
            if (arg == "--single-argument") { literalTail = true; forwarded.Add(arg); continue; }
            if (arg.StartsWith("--load-extension=", StringComparison.OrdinalIgnoreCase)) continue;
            if (arg.StartsWith("--user-data-dir=", StringComparison.OrdinalIgnoreCase)) explicitProfile = true;
            string value = arg;
            if (!arg.StartsWith("-", StringComparison.Ordinal) && !Uri.IsWellFormedUriString(arg, UriKind.Absolute) && File.Exists(arg))
                value = Path.GetFullPath(arg);
            forwarded.Add(value);
        }
        List<string> launch = new List<string>();
        if (!explicitProfile)
            launch.Add("--user-data-dir=" + Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), @"Frame Dev\User Data"));
        string extensionFile = InsideRoot(root, (string)config["extensionListFile"]);
        if (File.Exists(extensionFile))
        {
            string paths = File.ReadAllText(extensionFile, Encoding.UTF8).Trim();
            if (paths.Length != 0)
            {
                foreach (string folder in paths.Split(','))
                    if (!File.Exists(Path.Combine(folder, "manifest.json")))
                        throw new InvalidOperationException("A configured migrated extension is missing. Restore the preserved extension directory.");
                launch.Add("--load-extension=" + paths);
            }
        }
        launch.AddRange(forwarded);
        return new FrameLaunchPlan { Executable = executable, WorkingDirectory = release, Arguments = launch.ToArray() };
    }

    public static string JoinArguments(IEnumerable<string> args)
    {
        List<string> quoted = new List<string>();
        foreach (string arg in args)
        {
            StringBuilder text = new StringBuilder("\"");
            int slashes = 0;
            foreach (char c in arg)
            {
                if (c == '\\') { slashes++; continue; }
                if (c == '"') text.Append('\\', slashes * 2 + 1);
                else text.Append('\\', slashes);
                text.Append(c);
                slashes = 0;
            }
            text.Append('\\', slashes * 2);
            text.Append('"');
            quoted.Add(text.ToString());
        }
        return String.Join(" ", quoted.ToArray());
    }
}
