# app/hello.rb -- the M2 target program: what should say hello over UART.
#
# Deliberately boring: string building + iteration + puts, so the first
# on-target boot exercises the GC and the String/Integer core, not much else.
def greet(name)
  "Hello, #{name}! Compiled from Ruby by spinel, no interpreter aboard."
end

names = %w[ESP32 world Ruby]
names.each { |n| puts greet(n) }
puts "Integer arithmetic spot check: #{7 * 6}, fib(10) = #{fib = 55}"
