awfan 1.0.0-rc1
================

Native C++20 Alienware fan and thermal CLI for Windows.

Quick install
-------------
1. Extract the entire ZIP.
2. Open PowerShell in the extracted folder.
3. Run:

   .\install.ps1

4. Open a new terminal and run:

   awfan doctor
   awfan status

Portable use
------------
The package can be used without installation:

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
   awfan profile <0-5> --yes
   awfan auto <1-5> --yes

Every hardware-changing command requires --yes.

Boost values are firmware control inputs from 0 to 100. They are not fan
percentages and are not target RPM values. On the tested AC16251, a boost
value of 80 drove the fans close to their maximum speed. Use conservative
values and watch temperatures and RPM while testing.

RPM trend
---------
status, fans and watch compare consecutive RPM samples and report rising,
falling or stable. The reported maximum RPM is a nominal firmware value;
brief live readings above it are possible.

State
-----
awfan stores only its own last command and RPM sample history at:

   %LOCALAPPDATA%\awfan\state-v1.txt

Clear it with:

   awfan clear-state

PowerShell completion
---------------------
Load completion for the current session:

   . "$env:LOCALAPPDATA\Programs\awfan\awfan-completion.ps1"

Uninstall
---------
Run:

   & "$env:LOCALAPPDATA\Programs\awfan\uninstall.ps1"

Use -KeepState to retain the local command history file.

Compatibility
-------------
Validated on Alienware 16X Aurora AC16251 using the AWCC WMI provider in
ROOT\WMI. Other Alienware systems may expose different resources or methods.
