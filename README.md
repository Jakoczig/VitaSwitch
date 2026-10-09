# VitaSwitch

This is a small utility for the PS Vita that allows **fast switching between docked and portable configurations** for your plugins and system performance.

It automatically checks for the following configuration files and profiles:

- **PSVshell** (overclock plugin)
- **VitaGrafix**

and switches them alongside your `ur0:tai/config.txt` when they are present.

---

## How It Works

1. **Automatic First-Time Setup**
   - On the first run, the app creates portable and docked copies of your current configurations without overwriting existing mode presets.
   - After setup, the app **closes itself**.
   - New presets initially contain the same settings. Customize them to suit your docked and portable setups.

2. **Switching Between Modes**
   - Reads the current mode from `ur0:tai/switchstate.txt`:
     - `0` = portable
     - `1` = docked
   - Saves changes made to the active configuration and loads the other mode.
   - Updates the mode marker and **reboots automatically after a successful switch**.

3. **Dynamic Icon Feedback**
   - Updates the app icon and LiveArea background for the selected mode when the required artwork is available.
   - Keeps the alternative artwork files for future switches.

4. **Plugin Config Editing**
   - You can continue using **AutoPlugin 2** to edit the configuration for the active mode.
   - PSVshell and VitaGrafix configurations added after initial setup are checked on subsequent switches.

5. **Safer Switching in v1.22**
   - Checks file operations and prepares replacement files before switching.
   - Keeps recovery copies during the switch and attempts to restore the previous configuration if an operation fails before completion.
   - On the next launch after an interrupted switch, it attempts recovery and exits. Launch it again afterward to switch modes.

---

## Important Notes

- **Back up your configurations before installing or updating.**
  - Keep a separate copy outside the PS Vita. The app's mode presets are not a replacement for an independent backup.

- **A temporary black screen during switching is normal.**
  - The app has no in-app interface and requests a reboot after switching.
  - **Do not force a restart or interrupt power while it is working.** Recovery support does not make a multi-file switch immune to power loss.

- **If a switch fails:**
  - Check `ur0:tai/vitaswitch-error.txt` for the latest diagnostic message.
  - Do not delete `.vsw-old` recovery files. See the [recovery guide](docs/RECOVERY.md).

- **Supported configuration location:**
  - VitaSwitch uses `ur0:tai/config.txt`. It does not automatically switch an `ux0:tai` installation.

- **Use at your own risk.**
  - This app is provided as-is. The author takes no responsibility for issues, crashes, or data loss.

- **Coding disclaimer**
  - I have no coding knowledge, so future updates or fixes are not guaranteed.
  - Feel free to fork the project and improve it.

---

## Building and Testing

See [building and host tests](docs/BUILDING.md) for the VitaSDK build commands and regression tests.

---

## License

MIT License - see `LICENSE` for details.
