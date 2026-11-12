# app/audio.rb -- speaker and microphone from Ruby.
#
# M5StickS3: plays a short C-major scale on the speaker (ES8311 codec), then
# listens. M5StickC: listens (PDM microphone). Listening prints the sound
# level twice a second as a number and a bar -- clap or talk and it jumps --
# and draws the bar on the display when there is one.

module Audio
  ffi_func :board_name,          [], :str
  ffi_func :board_speaker_tone,  [:int, :int, :int], :int, blocking: true
  ffi_func :board_speaker_stop,  [], :int, blocking: true
  ffi_func :board_mic_level,     [:int], :int, blocking: true
  ffi_func :board_has_lcd,       [], :int
  ffi_func :esp32_display_init,  [], :int
  ffi_func :esp32_display_set_orientation, [:int], :int
  ffi_func :esp32_display_width, [], :int
  ffi_func :esp32_display_clear, [:int], :int
  ffi_func :esp32_display_rect,  [:int, :int, :int, :int, :int, :int], :int
  ffi_func :esp32_display_text,  [:str, :int, :int, :int, :int, :int], :int
end

SCALE = [262, 294, 330, 349, 392, 440, 494, 523]   # C4 .. C5

puts "board: #{Audio.board_name}"
rc = Audio.board_speaker_tone(SCALE[0], 250, 40)
if rc == 0
  puts "speaker: playing a C-major scale"
  i = 1
  while i < SCALE.size
    Audio.board_speaker_tone(SCALE[i], 250, 40)
    i += 1
  end
  Audio.board_speaker_tone(523, 600, 40)
  Audio.board_speaker_stop
else
  puts "speaker: none (#{rc})"
end

if Audio.board_mic_level(100) < 0
  puts "mic: none"
else
  puts "mic: listening -- clap or talk"
  lcd = Audio.board_has_lcd == 1 && Audio.esp32_display_init == 0
  w = 0
  if lcd
    Audio.esp32_display_set_orientation(1)
    w = Audio.esp32_display_width
    Audio.esp32_display_clear(0)
    Audio.esp32_display_text("MIC LEVEL", 2, 2, 0xFFFF, 0, 1)
  end
  while true
    lvl = Audio.board_mic_level(500)
    bars = lvl / 400
    bars = 40 if bars > 40
    puts "mic: #{lvl.to_s.rjust(5)} #{"#" * bars}"
    if lcd
      bw = (w - 4) * bars / 40
      Audio.esp32_display_rect(2, 20, w - 4, 20, 0x0000, 1)
      Audio.esp32_display_rect(2, 20, bw, 20, lvl > 8000 ? 0xF800 : 0x07E0, 1) if bw > 0
      Audio.esp32_display_text("#{lvl}     ", 2, 46, 0xFFE0, 0, 1)
    end
  end
end
