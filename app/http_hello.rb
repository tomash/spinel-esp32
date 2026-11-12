# app/http_hello.rb -- the smallest networked app: join WiFi through the
# net layer (firmware/main/net.h), then answer HTTP on port 80 with a
# one-line page, using spinel's TCPServer. A bring-up test for a new
# platform's socket path, before app/wifi_status.rb.
require "socket"

module Net
  ffi_func :board_name,         [], :str
  ffi_func :net_wifi_ssid,      [], :str
  ffi_func :net_wifi_init,      [], :int
  ffi_func :net_wifi_connect,   [], :int
  ffi_func :net_wifi_connected, [], :int
  ffi_func :net_wifi_ip,        [:ptr, :int], :int
  ffi_func :mote_millis,         [], :int
end

puts "http_hello on #{Net.board_name}: joining #{Net.net_wifi_ssid}"
rc = Net.net_wifi_init
puts "wifi init: #{rc}"
Net.net_wifi_connect
t0 = Net.mote_millis
while Net.net_wifi_connected == 0
  sleep 0.25
end
ip_buf = "\0" * 32
Net.net_wifi_ip(ip_buf, 32)
ip = ip_buf.delete("\0")
puts "connected in #{Net.mote_millis - t0} ms, ip #{ip}"

server = TCPServer.new("0.0.0.0", 80)
puts "listening: http://#{ip}/"
served = 0
while true
  client = server.accept
  req = client.read_nonblock(1024, exception: false)
  line = req.is_a?(String) ? req.split("\r\n").first.to_s : "(no request line)"
  served += 1
  body = "hello from spinel on #{Net.board_name}, request #{served}: #{line}\n"
  client.write "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: #{body.bytesize}\r\n\r\n#{body}"
  client.close
  puts "served #{served}: #{line}"
end
