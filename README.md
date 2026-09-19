A mobile weather station, consisting of a nRF9151-DK and a DFRobot Lark Weather Station.
This is a WIP project that is not yet functional.

You will need to patch the file `(workspace)/modules/hal/nordic/nrfx/bsp/stable/mdk/nrf9120_bitfields.h` as follows:
```
#define TWIM_FREQUENCY_FREQUENCY_K100 (0x0028F000UL) /*!< 100 kbps */
```
(it originally says 0x01980000UL)
The API is a bit braindead and only offers a few static speed settings, and 100k is the slowest, which is still way too fast if you use more than about 10mm of wire to attach the weatherstation. The above change redifines "100k" to actually mean "10k" and makes things work smoothly.


