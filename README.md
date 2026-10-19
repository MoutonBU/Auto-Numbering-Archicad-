# AutoNumber - Archicad plugin

Place an element and it gets the next number in its **Element ID**: 1, then 2, then 3 ...
A real Archicad Add-On: loads at startup and numbers at the moment of placement.

By default it numbers **Objects and Lamps** in one shared sequence. Change the list and the
prefix/padding at the top of `Src/AddOnMain.cpp`.

## Get the installer (.exe) - no programming tools needed

1. Create a free GitHub account and a new empty repository.
2. Upload everything in this folder to it (keep the folders, including the hidden `.github`).
3. Open the **Actions** tab. "Build AutoNumber" runs by itself (about 5-10 minutes).
4. Open the finished run. Under **Artifacts** download **AutoNumber-Setup-Archicad28**
   and unzip it. Inside is `AutoNumber-Setup-Archicad28.exe`.

## Install

Close Archicad, double-click `AutoNumber-Setup-Archicad28.exe`, click through, start Archicad.
(Windows may show a SmartScreen warning because the installer isn't code-signed:
More info > Run anyway.) Uninstall from Windows Settings > Apps.

If Archicad is installed somewhere unusual, pick its `Add-Ons` folder in the installer.
Without admin rights you can instead use Archicad: Options > Add-On Manager >
Edit List of Available Add-Ons > Add, and point it at a folder holding `AutoNumber.apx`
(the plain plugin zips are also in the Actions artifacts).

## Use

- It is on by default. Place an Object or Lamp, then check its Element ID.
- Toggle it with the menu command **Auto-Number Placed Elements** (checkmark = on).
- Copy/paste counts as a new placement and gets a new number.

## Build locally instead

Needs Visual Studio (Windows) or Xcode (Mac), CMake and Python 3.10+:

    python Tools/BuildAddOn.py --configFile config.json --acVersion 28 --buildConfig Release --package

Supported Archicad versions: 27, 28, 29, 30. The Mac plugin bundle goes into
`/Applications/GRAPHISOFT/Archicad NN/Add-Ons/`.
