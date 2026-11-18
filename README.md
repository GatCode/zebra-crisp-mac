# zebra-crisp-mac

**This driver makes Zebra label printers on macOS print sharp labels.** It is free and open source.

Apple's Zebra driver can print fuzzy labels from Chrome, Safari, Preview and other Mac apps. This
driver replaces the step that causes the problem. Labels print as sharp as raw ZPL.

![Before and after: the stock driver prints dotted, ragged text and barcode bars; zebra-crisp-mac prints clean edges](docs/before-after-scan.png)

*The image shows the same test label from each driver at 203 dpi, at 2x size. The Code 128 barcode
contains `CRISP-TEST-12345`. In our test, a scan-line decoder read the crisp barcode on all 180 rows.
It read the stock barcode on no rows.*

## Why labels print fuzzy

macOS makes each page in 1-bit black and white. It changes gray areas into patterns of dots
(dithering). Gray areas include the edges of text, barcodes and scaled images. At 203 dpi, these
dots make the edges rough. The stock driver has no setting to stop dithering.

This driver makes macOS send the page in 8-bit grayscale. The driver then changes each pixel to
black or white. It does not use dithering. A pixel that is darker than 25% gray prints black. Apple's
Zebra driver code then makes the ZPL. All other printer settings operate as before.

## Install

1. Download `ZebraCrisp-x.y.z.pkg` and `ZebraCrisp-x.y.z.pkg.sha256` from the
   [latest release](https://github.com/jcl5m1/zebra-crisp-mac/releases/latest).
2. Make sure that the checksum is correct. Open Terminal in the download folder and type:

   ```
   shasum -a 256 -c ZebraCrisp-x.y.z.pkg.sha256
   ```

   The result must be `OK`. If the result is not `OK`, do not install the package.
3. Open the package. If macOS blocks it, go to **System Settings > Privacy & Security** and click
   **Open Anyway**.
4. Select the default label size. The installer selects 4 x 6 in. To keep the current size of each
   printer, click **Keep Current**.

The installer moves all printers that use Apple's **Zebra ZPL Label Printer** driver to this
driver. It keeps their settings.

To add a new printer:

1. Go to **System Settings > Printers & Scanners > Add Printer**.
2. Select your Zebra printer.
3. In **Use**, select **Zebra ZPL Label Printer Crisp**.

### Shipping labels

- Select the size of your labels, for example **4.00 x 6.00"**. Print at 100% scale, not "fit". A
  4 x 6 label on a 4 x 5 setting prints smaller and less sharp.
- In Chrome, set **Margins** to **None**. If the label is sideways, set **Layout** to **Landscape**.
- **Line Weight** in the printer options sets the gray level that prints black:

  | Line Weight | Prints black |
  |---|---|
  | Thin | darker than 50% gray |
  | Normal (default) | darker than 25% gray |
  | Bold | darker than 12.5% gray |

  **Darkness** sets the heat of each dot.

## Preview the printed label

The print preview in an app shows the original page, not the printer output. To see the printer
output:

1. In the macOS print dialog, open the **PDF** menu and select **Preview Zebra Output**. In Chrome,
   click **Print using system dialog** first.
2. Preview opens the label as the printer will print it. It uses the label size and Line Weight of
   your Zebra printer.

## Uninstall

```
sudo /Library/Printers/ZebraCrisp/uninstall.sh
```

This script moves your printers back to Apple's Zebra ZPL driver and keeps their settings. It
removes all installed files.

## Compatibility

Tested with:

| | |
|---|---|
| macOS | 15.7.4 Sequoia (build 24G517) |
| Mac | Apple Silicon (arm64) |
| Printer | Zebra GX420d (USB), 4x6 direct thermal labels |
| Apps | Chrome |

The driver is for Zebra printers that use Apple's **Zebra ZPL Label Printer** driver, for example
GK420d, GX430t, ZP450, ZD410 and ZD420. The package also contains an Intel binary. We did not test
these printers or Intel Macs. Send reports in
[Issues](https://github.com/jcl5m1/zebra-crisp-mac/issues). The driver does not support Zebra EPL
and CPCL printers.

> **Note:** Apple deprecated PPD printer drivers, including its own Zebra driver. They operate on
> current macOS. A future macOS release can remove them.

## Installed files

| File | Function |
|---|---|
| `/Library/Printers/ZebraCrisp/rastertozebracrisp` | CUPS filter. Changes the grayscale page to black and white and sends it to Apple's `rastertolabel`. |
| `/Library/Printers/ZebraCrisp/uninstall.sh` | Uninstaller. |
| `/Library/PDF Services/Preview Zebra Output` | Item in the PDF menu of the print dialog. Shows the printer output. |
| `/Library/Printers/PPDs/Contents/Resources/ZebraCrisp.ppd` | Printer description. Apple's Zebra ZPL PPD, changed for 8-bit grayscale, this filter and the Line Weight option. |

## Build

You must have the Xcode Command Line Tools (`xcode-select --install`).

```
./build.sh 1.2.0
```

The script makes `dist/ZebraCrisp-1.2.0.pkg` and `dist/ZebraCrisp-1.2.0.pkg.sha256`.

To sign and notarize the package, set these variables before you run the script:

| Variable | Function |
|---|---|
| `DEVELOPER_ID_APPLICATION` | Signs the filter. Example: `Developer ID Application: Name (TEAMID)` |
| `DEVELOPER_ID_INSTALLER` | Signs the package. Example: `Developer ID Installer: Name (TEAMID)` |
| `NOTARY_PROFILE` | Notarizes and staples the package with this `notarytool` keychain profile. |

## License

[Apache License 2.0](LICENSE). The PPD comes from the CUPS Zebra ZPL driver by Apple Inc. (Apache
2.0). See [NOTICE](NOTICE).

Zebra is a trademark of Zebra Technologies. This project has no relation to Zebra Technologies or
Apple.
