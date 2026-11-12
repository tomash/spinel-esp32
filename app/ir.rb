# app/ir.rb -- infrared remote control (NEC) from Ruby.
#
# A board with an IR receiver (M5StickS3) listens and prints every NEC frame
# it decodes -- from another board running this app, or from most TV remotes
# (NEC is the common protocol). A board with only an IR LED (M5StickC) sends
# a frame every 2 s: address 0x5350 ("SP"), command counting up. A receiving
# board does not transmit: its own LED, millimetres from the receiver,
# overloads it. M5Stack recommends 30 cm or more, LED aimed at the receiver.

module IR
  ffi_func :board_name,        [], :str
  ffi_func :board_ir_init,     [], :int
  ffi_func :board_ir_send_nec, [:int, :int], :int
  ffi_func :board_ir_poll,     [:int], :int, blocking: true
  ffi_func :board_ir_addr,     [], :int
  ffi_func :board_ir_cmd,      [], :int
  ffi_func :board_led_init,    [], :int
  ffi_func :board_led_set,     [:int], :int
  ffi_source <<~C
    #include "board.h"
    int ir_has_tx(void) { int tx, rx; board_ir_pins(&tx, &rx); return tx >= 0; }
    int ir_has_rx(void) { int tx, rx; board_ir_pins(&tx, &rx); return rx >= 0; }
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    int ir_delay_ms(int ms) { vTaskDelay(pdMS_TO_TICKS((uint32_t)ms)); return 0; }
  C
  ffi_func :ir_delay_ms, [:int], :int, blocking: true
  ffi_func :ir_has_tx, [], :int
  ffi_func :ir_has_rx, [], :int
end

ADDR = 0x5350
puts "board: #{IR.board_name}"
rc = IR.board_ir_init
if rc != 0
  puts "ir: none (#{rc})"
else
  rx = IR.ir_has_rx == 1
  tx = IR.ir_has_tx == 1 && !rx
  puts "ir: #{tx ? "transmitter" : ""}#{tx && rx ? " + " : ""}#{rx ? "receiver" : ""}"
  IR.board_led_init
  cmd = 0
  while true
    if rx
      # listen for most of the 2 s cycle
      r = IR.board_ir_poll(1900)
      if r == 1
        puts "ir: received addr 0x#{IR.board_ir_addr.to_s(16)} cmd 0x#{IR.board_ir_cmd.to_s(16)}"
        IR.board_led_set(1)
      elsif r == 2
        puts "ir: repeat"
      elsif r == -1
        puts "ir: received something that is not NEC"
      end
      IR.board_led_set(0)
    end
    if tx
      s = IR.board_ir_send_nec(ADDR, cmd)
      puts "ir: sent addr 0x#{ADDR.to_s(16)} cmd 0x#{cmd.to_s(16)}#{s == 0 ? "" : " (error #{s})"}"
      cmd = (cmd + 1) % 256
      IR.board_ir_poll(0) if rx       # drop our own reflection
    end
    IR.ir_delay_ms(1900) unless rx    # a transmit-only board: one frame per 2 s
  end
end
