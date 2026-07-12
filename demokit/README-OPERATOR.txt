================================================================
  semantics-ar  DEMO KIT
  >>> TEST MODE - NOT production-signed. Throwaway demo VM only. <<<
================================================================

WHAT THIS IS
  A one-command setup that turns a disposable Windows VM into a working
  demo of semantics-ar (anti-ransomware). The driver is TEST-SIGNED, so the
  VM is put into Windows "test signing mode" - this is for demos only, until
  the production Microsoft signature is issued. Never run this on a real or
  work machine.

REQUIREMENTS
  - A throwaway Windows 10/11 VM you can reset (Hyper-V, VMware, VirtualBox, cloud).
  - Secure Boot OFF in the VM (a Generation-1 / BIOS VM avoids this entirely).
  - Administrator account in the VM.
  - No .NET runtime needed (the app is bundled self-contained).

THE THREE COMMANDS
  1) SET UP THE DEMO
       Double-click  Demo.cmd
     It checks the VM (GO/NO-GO), turns on test signing, reboots ONCE,
     then AUTOMATICALLY continues after you log back in: installs the
     product, seeds a safe attack sandbox, and opens the app.
     -> Ends on a green "DEMO READY" screen.

     (If it reports Secure Boot is ON: turn it off in the VM's settings, or
      use a Generation-1 VM, then run Demo.cmd again. Nothing was changed.)

  2) RUN THE ATTACK DEMO (safe, sandboxed)
       Double-click  attack\Demo-Attack.cmd
     Follow  attack\RUNBOOK.md  for the presenter script (ENFORCE blocks it;
     AUDIT records + recovers it). The attack only ever touches
     C:\SarDemo\Sandbox - it cannot reach real files.

  3) RETURN THE VM TO NORMAL
       Right-click  Reset-SarDemo.ps1  > Run with PowerShell
     Uninstalls the product, turns test signing off, one reboot.

GOOD TO KNOW
  - Re-running Demo.cmd after any interruption just continues where it left
    off (it detects what is already done).
  - A VM already prepared (test signing on) skips the reboot and installs
    immediately.
  - Check state any time:  powershell -File Start-SarDemo.ps1 -Status
================================================================
