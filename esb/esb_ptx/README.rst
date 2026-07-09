ESB Transmitter (PTX)
=====================

Based on the Nordic NCS ``samples/esb/esb_ptx`` sample for RC controller link bring-up.

Features
--------

* ESB PTX mode at 2 Mbps, sends control frames every 100 ms
* Sends ESB ``PAIR`` frame every 1 s with current radio address payload
* Receives aircraft status in ACK payloads (bidirectional link)
* UART RC link on the **console UART** (uart20), multiplexed with printk logs
* Pair with :file:`apps/esb/esb_prx` for end-to-end testing
* Pair with :file:`apps/bluetooth/xbox_central` (Ground BLE Hub) over a wired UART link

UART RC link (HUART-RC)
-----------------------

The PTX board receives stick/trigger channels from the Ground BLE Hub on the
same UART used for debug output. Binary frames use HDLC (flag ``0x7E``,
byte stuffing, FCS-16); ASCII log text between frames is ignored.

Wiring to Ground BLE Hub (nRF54L15 DK)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: text

   Hub uart30 TX  --->  PTX uart20 RX (DK P0.01)
   Hub uart30 RX  <---  PTX uart20 TX (DK P0.00)
   GND            <-->  GND

Both sides use **115200 baud** (see board overlays).

Protocol definition: :file:`apps/esb/common/uart_rc_link.h`

Message types beyond stick channels:

* ``TYPE_ESB_REQ/RSP (0x03/0x04)`` — radio config, pair, apply, save
* ``TYPE_DEBUG_CTRL/LOG (0x05/0x06)`` — enable log forwarding and stream text to Hub

Hub DK buttons (when wired to ESB PTX or PRX console UART):

* Button 3 — toggle ESB debug log forwarding
* Button 4 — ``PAIR`` PTX; press again after rewiring to PRX to sync/save addresses

Build
-----

.. code-block:: console

   cd apps/esb/esb_ptx
   west build -b nrf54l15dk/nrf54l15/cpuapp -p
   west flash

Flash this image to the transmitter DK and :file:`apps/esb/esb_prx` to a second DK.
When UART CTRL frames arrive from the Hub, local demo channels are overridden.
LED patterns on both boards should stay in sync.
