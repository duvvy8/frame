/* Locate only the browser owning this acceptance profile, never a user's window. */
const cp=require('node:child_process');
function frameWindowHandle(profile){
 const literal=profile.replace(/'/g,"''");
 const command=`$frameTestProfile='${literal}'; $frameTestProcesses=Get-CimInstance Win32_Process -Filter "Name='Frame.exe'" | Where-Object { $_.CommandLine -and $_.CommandLine.Contains($frameTestProfile) }; $frameTestWindows=@($frameTestProcesses | ForEach-Object { Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue } | Where-Object MainWindowHandle -ne 0); if ($frameTestWindows.Count -ne 1) { throw 'Expected one native window for the isolated Frame test profile' }; $frameTestWindows[0].MainWindowHandle`;
 return cp.execFileSync('powershell.exe',['-NoProfile','-Command',command],{encoding:'utf8',windowsHide:true}).trim();
}
module.exports={frameWindowHandle};
