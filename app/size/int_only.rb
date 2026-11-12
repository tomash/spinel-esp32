# app/size/int_only.rb -- footprint probe: Integer arithmetic and a method,
# no Strings, no puts, no collections. What would a "no-runtime" profile
# have to cover, and what does it cost today?
def fib(n)
  a = 0
  b = 1
  while n > 0
    t = a + b
    a = b
    b = t
    n -= 1
  end
  a
end

r = fib(20)
exit(r == 6765 ? 0 : 1)
