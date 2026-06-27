awfan 1.2.0
===========

Native C++20 Alienware fan and thermal CLI for Windows.

awfan is an independent community project and is not affiliated with or
endorsed by Dell Technologies or Alienware.

Install
-------
1. Extract the release ZIP.
2. Open PowerShell in the extracted folder.
3. Run:

   .\install.ps1

4. Approve the administrator prompt for the protected background broker.
5. Open a new terminal and verify:

   awfan version
   awfan broker status
   awfan doctor
   awfan status

The broker-enabled installation is stored in C:\Program Files\awfan. Every
hardware-changing command still requires --yes.

Named modes
-----------
Read the current mode:

   awfan mode
   awfan mode --json

Select a mode:

   awfan balanced --yes
   awfan balanced-performance --yes
   awfan cool --yes
   awfan quiet --yes
   awfan performance --yes

The unified form is also available:

   awfan mode cool --yes
   awfan mode 3 --yes

Mappings on the tested AC16251:

   1  0xA0  Balanced
   2  0xA1  Balanced Performance
   3  0xA2  Cool
   4  0xA3  Quiet
   5  0xA4  Performance

Manual control and restoration
------------------------------
Before manual boost, awfan remembers the active firmware profile. Return to it
with:

   awfan restore --yes

Temporary manual control can restore automatically:

   awfan boost 55 55 --yes --for 20m
   awfan max --yes --for 5m
   awfan boost 45 50 --yes --until-reboot

Durations support s, m, h, and d suffixes and are limited to seven days. The
background broker owns the timer, so closing the terminal does not cancel it.
The --until-reboot form restores after Windows next starts, not after a normal
broker restart.

Custom presets
--------------
Save and apply reusable raw firmware boost values:

   awfan preset create gaming 70 70
   awfan preset list
   awfan preset gaming --yes
   awfan preset gaming --yes --for 30m
   awfan preset delete gaming

Presets are stored in %LOCALAPPDATA%\awfan\presets.json.

Broker management
-----------------
   awfan broker status
   awfan broker status --json
   awfan broker restart
   awfan broker repair
   awfan broker logs
   awfan broker logs 100

Status reports frontend, core, and broker versions, the broker PID and uptime,
scheduled-task registration, and pipe reachability.

Diagnostics
-----------
Create a redacted JSON support report:

   awfan report
   awfan report .\my-report.json

The report includes Windows version, component versions, broker health, AWCC
discovery, telemetry, profiles, local state, and recent broker logs. Computer
names, usernames, and user-profile paths are redacted. Review any report before
sharing it publicly.

Updates and rollback
--------------------
   awfan update --check
   awfan update
   awfan update --force

The updater downloads the latest stable GitHub release, verifies its published
SHA-256 checksum, backs up the installed package, installs the update, and runs
version, broker, and AWCC health checks. If those checks fail, it attempts to
restore the previous version automatically.

The first update from an older release into 1.2.0 is still performed by that
older release's updater. Transactional rollback applies to updates started from
1.2.0 and later.

Monitoring
----------
   awfan status [--json]
   awfan fans [--json]
   awfan temps [once|seconds] [--json]
   awfan watch [seconds]
   awfan profiles [--json]
   awfan mode [--json]
   awfan doctor [--json]
   awfan state [--json]

Boost values are firmware inputs from 0 to 100. They are not target fan
percentages and are not target RPM values. Profile 0 remains diagnostic-only.

Portable installation
---------------------
Install without the scheduled broker:

   .\install.ps1 -NoBroker

This uses %LOCALAPPDATA%\Programs\awfan by default. AWCC commands may require an
elevated terminal in this mode.

Repair and uninstall
--------------------
Repair the protected installation:

   awfan broker repair

Uninstall:

   & "C:\Program Files\awfan\uninstall.ps1"

Use -KeepState to retain local awfan state and presets.

Compatibility
-------------
Validated on Alienware 16X Aurora AC16251 using ROOT\WMI and
AWCCWmiMethodFunction. Other Alienware systems may expose different resources
or firmware behavior.

License and notices
-------------------
awfan is released under the MIT License. See LICENSE.txt.
Third-party acknowledgements are listed in THIRD-PARTY-NOTICES.txt.
