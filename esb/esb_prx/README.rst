ESB Receiver (PRX)
==================

Based on the Nordic NCS ``samples/esb/esb_prx`` sample for RC controller link bring-up.

Features
--------

* ESB PRX mode at 2 Mbps, receives control frames from PTX
* Sends aircraft status back to PTX inside ACK payloads
* Pair with :file:`apps/esb/esb_ptx` for end-to-end testing

Build
-----

.. code-block:: console

   cd apps/esb/esb_prx
   west build -b nrf54l15dk/nrf54l15/cpuapp -p
   west flash

Flash this image to the receiver DK and :file:`apps/esb/esb_ptx` to a second DK.
LED patterns on both boards should stay in sync.
