# VitaSwitch

This is a small utility for the PS Vita that allows **fast switching between docked and portable configurations** for your plugins and system performance.  

It **dynamically detects** if the following are installed:  

- **PSVshell** (overclock plugin)  
- **VitaGrafix**  

and will switch only the relevant configurations if they are present.  

---

## How It Works

1. **Automatic First-Time Setup**  
   - On the first run after installation, the app creates the required backup copies of your plugin/config files.  
   - After creating the backups, the app **closes itself**.  
   - Once this setup is done, you can switch between modes at any time.

2. **Switching Between Modes**  
   - Reads the current mode from `ur0:tai/switchstate.txt`:  
     - `0` = portable  
     - `1` = docked  
   - Renames configuration files for the next mode.  
   - Updates `switchstate.txt` to reflect the new mode.  
   - Reboots the system automatically to apply the changes.  

3. **Dynamic Icon Feedback**  
   - The app dynamically changes its icon to indicate the **current mode** (portable or docked).  
   - This helps you quickly see which mode is active without opening the app.  

4. **Plugin Config Editing**  
   - You can still use **AutoPlugin 2** normally to edit your plugin configurations during the selected mode.  

---

## Important Notes

- **Black screen during switch is normal.**  
  - The app renames configuration files and triggers a reboot, so a black screen will appear temporarily.  
  - **Never force restart during this process**, as it may corrupt your `tai/config.txt`.  

- **Use at your own risk.**  
  - This app is provided as-is.  
  - The author takes **no responsibility** for any issues, crashes, or data loss.  

- **Coding disclaimer**  
  - I have **no coding knowledge**, so there may be no future updates or fixes.  
  - Feel free to **fork** the project and improve it if you wish.  

---

## License

MIT License – see `LICENSE` for details.
