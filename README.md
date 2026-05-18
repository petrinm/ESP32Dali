# ESP32Dali

ESP32Dali is an ESP32-based, [ESPHome](https://esphome.io/)-friendly interface to [DALI](https://en.wikipedia.org/wiki/Digital_Addressable_Lighting_Interface) lighting devices. It acts as a master on the DALI bus through an isolated front-end, and can also provide 18 V DALI supply voltage from 5 V input.

The board is a matchbox-sized 2-layer PCB that hosts an ESP32-WROOM-32 plus the DALI interface.

- Schematic & PCB: `hw/` ([KiCanvas](https://kicanvas.org/?github=https%3A%2F%2Fgithub.com%2Fpetrinm%2FESP32Dali%2Fblob%2Fmain%2Fhw%2FESPDali.kicad_sch), [PDF](https://github.com/petrinm/ESP32Dali/blob/main/hw/ESPDali.pdf))
- 3D STEP and printable enclosure: `3d/`
- ESPHome external components: `components/`

![ESP32Dali](https://raw.githubusercontent.com/petrinm/ESP32Dali/refs/heads/main/hw/ESP32Dali.png)
![ESP32Dali front side](https://raw.githubusercontent.com/petrinm/ESP32Dali/refs/heads/main/hw/ESPDali_front.jpg)
![ESP32Dali back side](https://raw.githubusercontent.com/petrinm/ESP32Dali/refs/heads/main/hw/ESPDali_back.jpg)


## Example ESPHome configuration

The `dali` component can be pulled in as an [external component](https://esphome.io/components/external_components), so the same configuration works on the ESP32Dali board and on any other ESP32 with a compatible DALI front-end.

```yaml
esphome:
  name: livingroom
  friendly_name: Livingroom

esp32:
  board: esp32dev
  framework:
    type: arduino

external_components:
  - source:
      type: git
      url: https://github.com/petrinm/ESP32Dali.git
    components: [ dali ]

dali:
  id: dali_if
  tx_pin:
    number: 17
    inverted: true
  rx_pin:
    number: 16
    inverted: true

light:
  # Dim-only gear on one short address:
  - platform: dali
    name: "LED strip"
    interface: dali_if
    address: 5
    fadetime: 7
    faderate: 2

  # Dual-channel tunable white (two DAPC channels, two short addresses):
  # - platform: dali
  #   name: "TW panel"
  #   interface: dali_if
  #   cold_white_address: 0
  #   warm_white_address: 1
  #   cold_white_color_temperature: 6536 K
  #   warm_white_color_temperature: 2000 K
  #   fadetime: 7
  #   faderate: 2

  # DT8 colour gear on one address (DAPC + DT8 colour temperature command):
  # - platform: dali
  #   name: "DT8 lamp"
  #   interface: dali_if
  #   address: 5
  #   cold_white_color_temperature: 6536 K
  #   warm_white_color_temperature: 2000 K
```

Build and flash with:

```bash
$ esphome run livingroom.yaml
```


## License

Released under the MIT License.
