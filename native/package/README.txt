awfan 1.0.0-rc1
================

Native C++20 Alienware fan and thermal CLI for Windows.

Quick install
-------------
1. Extract the ZIP.
2. Open PowerShell in the extracted folder.
3. Run:

   .\install.ps1

4. Open a new terminal and run:

   awfan doctor
   awfan status

Portable use
------------
   .\awfan.exe status

Read commands
-------------
   awfan status [--json]
   awfan fans [--json]
   awfan temps [once|seconds] [--json]
   awfan watch [seconds]
   awfan profiles [--json]
   awfan doctor [--json]
   awfan state [--json]

Experimental control commands
-----------------------------
   awfan boost <cpu-value> <gpu-value> --yes
   awfan max --yes
   awfan profile <1-5> --yes
   awfan auto <1-5> --yes

Every control command requires --yes.

Boost values are raw firmware inputs from 0 to 100. They are not percentages
and are not target RPM values. A boost command selects manual control. Use a
discovered profile from 1 to 5 to return to dynamic firmware control.

Profile 0 is shown by the profiles command for diagnostics but is intentionally
not accepted by the profile command.

RPM trend
---------
status, fans and watch compare consecutive RPM samples and report rising,
falling or stable. The reported maximum RPM is nominal; brief readings above
it are possible.

State
-----
awfan stores its last command and RPM sample history at:

   %LOCALAPPDATA%\awfan\state-v1.txt

Clear it with:

   awfan clear-state

PowerShell completion
---------------------
   . "$env:LOCALAPPDATA\Programs\awfan\awfan-completion.ps1"

Uninstall
---------
   & "$env:LOCALAPPDATA\Programs\awfan\uninstall.ps1"

Use -KeepState to retain the local state file.

Compatibility
-------------
Validated on Alienware 16X Aurora AC16251 using the AWCC WMI provider in
ROOT\WMI. Other Alienware systems may expose different resources or methods.
