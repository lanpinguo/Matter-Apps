ESB Transmitter (PTX)
=====================

Based on the Nordic NCS ``samples/esb/esb_ptx`` sample for RC controller link bring-up.

Features
--------

* ESB PTX mode at 2 Mbps; forwards Hub UART CTRL frames over ESB
* On Hub ``PAIR`` (Btn4): generate addresses, SAVE, broadcast OTA ``PAIR`` until PRX ACK (max 30 s)
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

   Hub uart30 TX (P0.00)  --->  PTX uart20 RX (P1.05)
   Hub uart30 RX (P0.01)  <---  PTX uart20 TX (P1.04)
   GND                    <-->  GND

Only **TX, RX, and GND** are required. RTS/CTS are not used by HUART-RC.
Both sides use **115200 baud** (see board overlays).

Protocol definition: :file:`apps/esb/common/uart_rc_link.h`

Message types beyond stick channels:

* ``TYPE_ESB_REQ/RSP (0x03/0x04)`` — radio config, pair, apply, save
* ``TYPE_DEBUG_CTRL/LOG (0x05/0x06)`` — enable log forwarding and stream text to Hub

Hub DK buttons (when wired to ESB PTX console UART):

* Button 4 hold 1.5 s — ``PAIR`` PTX: generate addresses, SAVE, broadcast OTA
  ``PAIR`` until PRX ACK (max 30 s; PRX must be in pair mode)
* Button 3 short press — optional UART sync of the same addresses to PRX (rewire Hub UART)
* Button 3 hold 1.5 s — toggle ESB debug log forwarding

OTA pairing
^^^^^^^^^^^

1. Put PRX in pair mode (first boot with no saved config, or hold PRX button 4 for 5 s).
2. Keep Hub UART wired to PTX; hold Hub **button 4** for 1.5 s.
3. Within the window PRX should ACK the PAIR frame; PTX then immediately enters
   UART CTRL forward mode on the new addresses (falls back to 30 s timeout).

Build
-----

.. code-block:: console

   cd apps/esb/esb_ptx
   west build -b nrf54l15dk/nrf54l15/cpuapp -p
   west flash

Flash this image to the transmitter DK and :file:`apps/esb/esb_prx` to a second DK.
ESB control frames are sent only while UART CTRL frames arrive from the Hub
(500 ms link timeout). No onboard demo channels are transmitted.
LED patterns on both boards should stay in sync.
