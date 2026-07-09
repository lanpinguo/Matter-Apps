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

PTX and PRX must use the **same ESB addresses**.

1. Wire Hub ``uart30`` to PTX console UART, press Hub **button 4** to ``PAIR`` the transmitter.
2. Rewire Hub UART to PRX console UART, press Hub **button 4** again to ``SET_ADDR`` /
   ``APPLY`` / ``SAVE`` the paired addresses on the receiver.
3. Remove the bench UART wire; airborne link uses ESB only.

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
