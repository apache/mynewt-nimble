# BLE Servo peripheral app.

The source files are located in the src/ directory.

pkg.yml contains the base definition of the app.

syscfg.yml contains setting definitions and overrides.

On default servo PWM channel is connected to pin 31 of nRF52840-DK board,
and it can be changed in syscfg.yml file (make sure that chosen pin supports PWM).
