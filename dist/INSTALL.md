# Install Artemis on a Nintendo 3DS

`artemis.cia` is the normal install package. Installing it with FBI adds an
Artemis icon to the 3DS HOME Menu, just like any other installed title.

## What you need

- A 3DS, 3DS XL, New 3DS, or New 3DS XL with custom firmware.
- [FBI](https://github.com/Steveice10/FBI) installed on the console.
- The `artemis.cia` file from this folder.
- An SD card reader, or a supported wireless file-transfer method.

## Install with FBI

1. Power off the 3DS and put its SD card in your computer.
2. Copy `artemis.cia` anywhere on the SD card. A `cias` folder is a tidy
   choice, but it is not required.
3. Safely eject the SD card, return it to the 3DS, and power the console on.
4. Open **FBI** from the HOME Menu.
5. Choose **SD** and browse to the folder containing `artemis.cia`.
6. Select `artemis.cia`, then choose **Install and delete CIA**. Choose
   **Install CIA** instead if you want to keep the installer file on the SD
   card.
7. Wait for the success message, press **A**, then press **HOME**. Artemis
   now appears on the HOME Menu and can be launched normally.

If FBI reports an install error, confirm that the SD card has enough free space
and that the CIA finished copying before removing the card. Do not install CIA
files on a stock, unmodified console.

## Homebrew Launcher alternative

`artemis.3dsx` is for the Homebrew Launcher and does not create a HOME Menu
icon. Copy it to `sd:/3ds/artemis/artemis.3dsx`, then launch it through the
Homebrew Launcher. Use the CIA method above if you want the normal HOME Menu
experience.
