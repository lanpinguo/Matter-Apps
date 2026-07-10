ESB Receiver (PRX)
==================

Based on the Nordic NCS ``samples/esb/esb_prx`` sample for RC controller link bring-up.

Features
--------

* ESB PRX mode at 2 Mbps, receives control frames from PTX
* Sends aircraft status back to PTX inside ACK payloads
* UART RC link on the **console UART** for ESB pair/config/save (bench setup)
* Radio addresses persisted in flash (``esb_prx/radio`` settings key)
* If no saved config exists, PRX enters auto-pair mode and accepts the first valid ESB ``PAIR`` frame
* Pair with :file:`apps/esb/esb_ptx` for end-to-end testing

Pairing workflow
----------------

PTX and PRX must use the **same ESB addresses**. Preferred path is **OTA**:

1. Put PRX in pair mode (no saved ``esb_prx/radio``, or hold PRX **button 4** for 5 s).
2. Wire Hub ``uart30`` to PTX console UART, hold Hub **button 4** for 1.5 s.
3. PTX generates addresses, saves them, and broadcasts ESB ``PAIR`` on the
   default listen address until PRX ACKs (max 30 s). PRX accepts the first
   valid frame, saves, and PTX switches to UART CTRL forward immediately.
4. Remove the bench UART wire if desired; airborne link uses ESB only.

Alternate bench path (UART sync to PRX):

1. Press Hub **button 4** while wired to PTX (caches addresses on Hub).
2. Rewire Hub UART to PRX, press Hub **button 3** to ``SET_ADDR`` / ``APPLY`` / ``SAVE``.

Radio clear (long press)
-------------------------

On the PRX DK, **press and hold button 4 for 5 seconds** to delete the persisted
``esb_prx/radio`` settings and re-enter auto-pair mode (wait for the first
valid ESB ``PAIR`` frame).

Protocol definition: :file:`apps/esb/common/uart_rc_link.h`

Build
-----

.. code-block:: console

   cd apps/esb/esb_prx
   west build -b nrf54l15dk/nrf54l15/cpuapp -p
   west flash

Flash this image to the receiver DK and :file:`apps/esb/esb_ptx` to a second DK.
LED patterns on both boards should stay in sync.
