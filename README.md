# NixieDMM Thermometer

NixieDMM Thermometer turns a vintage Bell & Howell Schools digital multimeter
into an internet connected thermometer while keeping the meter's Nixie
tube display as the readout.

An ESP32-C3 SuperMini fetches the current temperature from OpenWeatherMap and
controls a 100 kΩ X9C104 digital potentiometer. The multimeter reads the digipot
output in its kΩ mode. Each of the digipot's 100 positions represents approximately
1 kΩ, producing the resistance steps that make the DMM's Nixie tubes display temperatures.

The 100 kΩ digipot was selected to cover ~15–115 °F, which covers most common fahrenheit temperatures, at least in Austin 🤠 The firmware also rolls the
display digits during configured overnight hours to reduce uneven Nixie cathode
wear.

## How it works

1. At startup, the ESP32 creates a temporary Wi-Fi configuration network.
2. After connecting to Wi-Fi, it requests the current temperature for the configured
   location from OpenWeatherMap.
3. The firmware converts the whole-number Fahrenheit temperature into a calibrated
   digipot position.
4. The Bell & Howell DMM measures the resulting resistance on its 200 kΩ scale and
   shows the value on its Nixie display.
5. The temperature is refreshed once per minute. During the configured dead hours,
   the firmware periodically cycles the digits before restoring the last temperature.

## Hardware

- Bell & Howell Schools Nixie digital multimeter: these can
  often be found on eBay or local online marketplaces
- ESP32-C3 SuperMini development board: these are very cheap on AliExpress. Another
  ESP32-C3 board should work too, just make sure to change its PlatformIO board definition
  and assign suitable GPIO pins
- X9C104 100 kΩ digital potentiometer
- Isolated 120 VAC-to-5 VDC supply for the original internal-power arrangement
  (qualified mains work only; an external supply is strongly preferred and much safer)

The original build powers the ESP32 from an internal 120 VAC-to-5 VDC supply wired
to the DMM power cord. I do not recommend trying this as 120 VAC is fatal if not handled correctly.

## Wiring

| Net | ESP32-C3 SuperMini | X9C104 | DMM connection | Purpose |
| --- | --- | --- | --- | --- |
| `+5V` | `5V` | Pin 8 `VCC` | — | Powers the ESP32 board and digipot |
| `COMMON` | `GND` | Pin 4 `GND` and pin 6 `RL` | Black lead in `C`/COM jack | Digital ground and low end of the measured resistance |
| `INC` | GPIO 2 | Pin 1 `INC` | — | Wiper-step pulse |
| `U_D` | GPIO 1 | Pin 2 `U/D` | — | Selects increasing or decreasing wiper movement |
| `CS` | GPIO 7 | Pin 7 `CS` | — | Selects the digipot |
| `DMM_SENSE` | — | Pin 5 `RW` | White lead in `mA/Ω` jack | Wiper output measured by the DMM |
| `UNUSED_HIGH` | — | Pin 3 `RH` | — | Leave unconnected in this rheostat configuration |

This arrangement measures resistance between `RW` and `RL`. Moving the wiper upward
increases that resistance. If you use another ESP32-C3 board, change the three GPIO
constants in `include/config.h` to match your wiring. Check the markings and
datasheet for your physical X9C104 package first before blindly trusting these!

## DMM selector settings

The Bell & Howell meter has two selectors that must both be set to the correct settings:

1. Set the left selector to **kΩ**, at the position labeled `kΩ (200Ω)`.
2. Set the right selector to **200**, the fourth resistance-range option, not
   the first option labeled `(200Ω)`.
3. Plug the black test lead into the `C`/COM jack and the white test lead into the
   `mA/Ω` jack, then connect them as shown in the wiring table.

## Calibrating the displayed value

The temperature-to-position offsets in `updateDigipot()` are calibrated for the
original meter. To calibrate another unit, step through all 100 X9C104 positions and
record what the DMM actually displays at each position. Use that table to determine
the correction between the requested display value and the required wiper position.

The correction may not be constant across the full range. As the intended value
increases, a larger subtraction may be needed because advancing the desired value
by one can sometimes advance the value shown by the DMM by two. Adjust the four
range-based subtraction values in `updateDigipot()` to match the measured behavior
of your meter.

## Configuration before programming

The repository contains `include/config.example.h`. Make a
private copy named `include/config.h`:

```sh
cp include/config.example.h include/config.h
```

Edit `include/config.h` and set:

- `OPENWEATHER_API_KEY`: your OpenWeatherMap API key. Get your own for free [here](https://openweathermap.org/api).
- `WEATHER_LOCATION`: the query accepted by OpenWeatherMap, such as `Austin,US`.
- `UTC_OFFSET_SECONDS`: the fixed local offset from UTC.
- `DIGIPOT_INC_PIN`, `DIGIPOT_UD_PIN`, and `DIGIPOT_CS_PIN`: the GPIO pins used by
  your wiring.
- `DIGIPOT_RESISTANCE_OHMS`: the nominal digipot resistance; the supplied value is
  100 kΩ.
- `DEAD_HOURS_START` and `DEAD_HOURS_END`: local hours during which digit rolling is
  allowed.

## Captive portal and Wi-Fi setup

Every time the device starts, it creates the Wi-Fi network
`DVMThermometerConfig` and opens a captive configuration page. Connect to that
network, open the page if it does not appear automatically, enter the destination
Wi-Fi SSID and password, and select **Submit**.

The portal waits for a client for 30 seconds after startup. If nobody is connected at
that point, the access point closes and the ESP32 continues using any previously
saved credentials. If a client is connected when the timeout expires, the portal
stays available until the form is submitted. Submitting saves the credentials,
starts the Wi-Fi connection, and closes the portal.

## Programming with PlatformIO

Install [Visual Studio Code](https://code.visualstudio.com/) and the PlatformIO IDE
extension, or install PlatformIO Core for command-line use. Then:

1. Clone or download this repository.
2. Open the repository folder as a PlatformIO project.
3. Create and edit `include/config.h` as described above.
4. Connect the ESP32-C3 SuperMini with a data-capable USB cable.
5. Build the firmware:

   ```sh
   pio run
   ```

6. Upload it to the board:

   ```sh
   pio run --target upload
   ```

7. Optionally watch startup and diagnostic messages at 115200 baud:

   ```sh
   pio device monitor --baud 115200
   ```

The `platformio.ini` file selects the `nologo_esp32c3_super_mini` board and installs
the NTPClient and X9C10X libraries automatically. If upload does not begin, put your
specific ESP32-C3 board into its bootloader mode and retry; the button sequence can
vary by board revision.

## Project structure

```text
NixieDMMThermometer/
├── include/
│   ├── config.example.h        # Committed configuration template
│   └── config.h                # Private local settings; ignored by Git
├── src/
│   └── NixieDMMThermometer.cpp # Firmware and captive portal
├── .gitignore
├── LICENSE                     # MIT License
├── platformio.ini              # Board, framework, and library configuration
└── README.md
```

Build output is written to `.pio/`, which is also ignored by Git.

## License

This project is available under the [MIT License](LICENSE).
