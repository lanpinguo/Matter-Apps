ESB Transmitter (PTX)
=====================

Based on the Nordic NCS ``samples/esb/esb_ptx`` sample for RC controller link bring-up.

Features
--------

* ESB PTX mode at 2 Mbps, sends control frames every 100 ms
* Receives aircraft status in ACK payloads (bidirectional link)
* Pair with :file:`apps/esb/esb_prx` for end-to-end testing

Build
-----

.. code-block:: console

   cd apps/esb/esb_ptx
   west build -b nrf54l15dk/nrf54l15/cpuapp -p
   west flash

Flash this image to the transmitter DK and :file:`apps/esb/esb_prx` to a second DK.
LED patterns on both boards should stay in sync.
