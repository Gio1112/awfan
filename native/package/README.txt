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
   awfan presets
   awfan doctor [--json]
   awfan state [--json]

Experimental control commands
-----------------------------
   awfan boost <cpu-value> <gpu-value> --yes
   awfan max --yes
   awfan profile <1-5> --yes
   awfan auto <1-5> --yes

Every control command requires --yes.

Boost values are firmware fan-boost inputs from 0 to 100. They are not target
fan percentages and are not target RPM values. A boost command selects manual
control. Use a discovered profile from 1 to 5 to return to dynamic firmware
control.

Known profile names for the tested AC16251 are:

   1  0xA0  Balanced
   2  0xA1  Balanced Performance
   3  0xA2  Cool
   4  0xA3  Quiet
   5  0xA4  Performance

Run awfan profiles and awfan presets before changing profiles. Profile 0 is
shown for diagnostics but is intentionally not accepted by the profile command.

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
