awfan 1.1.0
===========

Native C++20 Alienware fan and thermal CLI for Windows.

awfan is an independent community project and is not affiliated with or
endorsed by Dell Technologies or Alienware.

Quick install
-------------
1. Extract the ZIP.
2. Open PowerShell in the extracted folder.
3. Run:

   .\install.ps1

4. Approve the one administrator prompt used to install the background broker.
5. Open a new terminal and run:

   awfan broker-status
   awfan doctor
   awfan status

Background broker
-----------------
The installer creates an elevated scheduled task for the current Windows user.
The task starts awfan-broker.exe at sign-in and keeps AWCC access in the
background. Normal awfan commands are sent through a named pipe restricted to
the current user, Administrators, and SYSTEM.

This removes repeated UAC prompts for status, monitoring, profiles, and fan
control. Every hardware-changing command still requires --yes.

The broker pauses while Windows is asleep and continues after resume. Closing
the lid only keeps it active when Windows is configured not to sleep.

Check it with:

   awfan broker-status

Portable or broker-free installation
-------------------------------------
The package can still be used directly from an elevated terminal. To install
without the scheduled background broker:

   .\install.ps1 -NoBroker

Hardware commands may then require an elevated terminal.

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

Updates
-------
Check for a newer stable GitHub release:

   awfan update --check

Download, verify, and install the latest stable release:

   awfan update

The updater verifies the release SHA-256 checksum. Updates that include the
broker may request one administrator approval while replacing and restarting
the scheduled task.

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

State
-----
awfan stores its last command and RPM sample history at:

   %LOCALAPPDATA%\awfan\state-v1.txt

Clear it with:

   awfan clear-state

Uninstall
---------
   & "$env:LOCALAPPDATA\Programs\awfan\uninstall.ps1"

The uninstaller removes the scheduled broker task. Use -KeepState to retain the
local state file.

Compatibility
-------------
Validated on Alienware 16X Aurora AC16251 using the AWCC WMI provider in
ROOT\WMI. Other Alienware systems may expose different resources or methods.

License and notices
-------------------
awfan is released under the MIT License. See LICENSE.txt.
Third-party acknowledgements are listed in THIRD-PARTY-NOTICES.txt.
