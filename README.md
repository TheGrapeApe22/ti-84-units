# units for TI-84 Plus CE

modeled after the amazing unit calculator, GNU Units!!

featuring...
![demo image](demo.png)

### Quick install
1. [Jailbreak](https://yvantt.github.io/arTIfiCE/) your calculator if its OS version >=5.5
2. use [TI Connect CE](https://education.ti.com/en/products/computer-software/ti-connect-ce-sw) to upload the files from `bin/` to your calculator.
3. To run, select `prgm`, `UNITS`.

### Usage

Enter a quantity at **You have**, then a target unit at **You want**. Leave the target blank to show the definition. For example, `3 ft + 6 inch` to `m`. A unit suffix such as `cm3` means `cm^3`. In `a / b c d`, the denominator is `b*c*d`; explicit multiplication after that is evaluated left to right.

The top-row keys open these pages:

- **F1 (Y=):** Quit and save history and settings.
- **F2 (Window):** Create or edit up to eight session variables. Enter a letter-only name, then its value as a unit expression. Up/Down selects a variable; Enter edits it; Del or Clear removes it. Variables last until the app exits.
- **F3 (Zoom):** Browse the transferred `UNITDB` database. Type to filter lines immediately; Up/Down scrolls the matches.
- **F4 (Trace):** Help and expression rules.
- **F5 (Graph):** Switch dark/light theme, choose 2-7 significant digits, and view credits. Theme and precision are archived and restored on the next launch. Precision applies to new results; the eight-character number limit can reduce it.

Mode returns to the calculator from another page. The same F-key toggles its page. Alpha toggles letters and numbers/operators; 2nd makes the next letter uppercase. Left/Right move the input cursor. Up/Down selects history, Enter recalls it, and Del or Clear removes the selected entry. The latest six history entries are archived on normal exit and survive a RAM clear. 2nd+On also quits.

### Workflow
* Install [CE C/C++ Toolchain](https://ce-programming.github.io/toolchain/static/getting-started.html)
* Install [CeMU emulator](https://github.com/CE-Programming/CEmu/releases/tag/v2.0) for easy testing (requires a ROM dump from your physical calculator)
* `make` to compile to `bin/`

Note: `bin/clibs.8xg` is a library downloaded from [here](https://tiny.cc/clibs)