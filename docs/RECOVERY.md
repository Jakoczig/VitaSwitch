# VitaSwitch recovery

This guide describes VitaSwitch v1.22.

## Before doing anything manually

Keep an independent backup of the complete affected folders, including the
active files, both mode presets, transaction markers, and all `.vsw-*` files.
Do not delete recovery files merely to make the next launch continue.

VitaSwitch manages these locations:

| Component | Active configuration | Parked mode configurations |
| --- | --- | --- |
| taiHEN | `ur0:tai/config.txt` | `config_portable.txt`, `config_docked.txt` in the same folder |
| PSVshell | `ur0:data/PSVshell/profiles` | `profiles_portable`, `profiles_docked` in the same folder |
| VitaGrafix | `ux0:data/VitaGrafix/config.txt` | `config_portable.txt`, `config_docked.txt` in the same folder |

The current mode marker is `ur0:tai/switchstate.txt`: exactly `0` or `1`.
The latest diagnostic message is in `ur0:tai/vitaswitch-error.txt`.

## Automatic recovery

If the app can start, its next launch checks for an interrupted transaction
before attempting a new switch. It restores an uncommitted switch or finishes
cleanup for a committed one, then exits without toggling again. Read the
diagnostic message and launch the app again only after recovery succeeds.

The relevant markers are `ur0:tai/vitaswitch.transaction`,
`ur0:tai/vitaswitch.committed`, and `ur0:tai/vitaswitch.committed.tmp`.
Keep them with the recovery files; they determine how the app interprets an
interrupted operation.

## When manual inspection is necessary

A switch spans multiple files and directories. A power cut can occur after an
active path is moved aside but before the replacement takes its place.
In particular, a missing `ur0:tai/config.txt` may interfere with plugin loading
and prevent normal use of the app.

A `.vsw-old` suffix identifies an original item retained during a switch.
For example, `ur0:tai/config.txt.vsw-old` can hold the previous active
configuration while `ur0:tai/config.txt` is temporarily absent.

Using an available file manager or recovery method, first preserve all files.
If the active path is absent, inspect the corresponding `.vsw-old` item before
restoring it to its original name. If both paths exist, do not overwrite either
blindly: they may represent different modes or different transaction stages.
Treat PSVshell profile directories as complete directory trees, not individual
files. Restoring only taiHEN does not establish that all components agree on
the selected mode.

Do not guess a new mode marker, erase transaction markers, or remove unknown
scratch files to bypass a failed recovery. Resolve the configuration and marker
state together, or restore a complete known-good independent backup.

Artwork uses separate `.vsw-art` and `.vsw-art-old` files. An artwork error can
occur after the configuration switch has committed; it does not by itself
prove that the plugin configuration switch failed.

## Limits

Host tests simulate errors and process interruptions. They do not prove behavior
under every real storage failure or firmware configuration, and the transaction
is not a single atomic filesystem operation. Retain an off-device backup even
after normal switching has been tested successfully.
