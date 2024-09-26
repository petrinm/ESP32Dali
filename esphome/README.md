# Example configuration

```yaml
light:
  - platform: monochrome
    name: "LED strip"
    output: dali_output

dali:
  id: dali_if
  tx_pin:
    number: 17
    inverted: true
  rx_pin:
    number: 16
    inverted: true

output:
  - platform: dali
    id: dali_output
    interface: dali_if
    address: 5

```

Run
```bash
$ esphome run livingroom.yaml
```




## Installing ESP32Dali as external component

https://esphome.io/components/external_components


```yaml
external_components:
  - source:
      type: git
      url: https://github.com/petrinm/ESP32Dali.git
    components: [ dali ]
```


