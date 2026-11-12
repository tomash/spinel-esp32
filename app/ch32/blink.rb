# app/ch32/blink.rb -- bare-metal blink for the CH32V203 (no RTOS, no SDK).
#
# Registers poked directly: RCC APB2 clock enable, GPIOA CRL, OUTDR.
# The EVT board's LED is wired by jumper; PA0 here. Delay is a busy loop.

module CH32
  ffi_source <<~C
    #include <stdint.h>
    #define REG(a) (*(volatile uint32_t *)(a))
    int ch32_led_init(int pin) {
        REG(0x40021018) |= 1u << 2;                     /* RCC_APB2PCENR: IOPA */
        uint32_t crl = REG(0x40010800);
        crl &= ~(0xFu << (pin * 4));
        crl |= 0x3u << (pin * 4);                       /* out push-pull 50 MHz */
        REG(0x40010800) = crl;
        return 0;
    }
    int ch32_led_write(int pin, int on) {
        REG(0x40010810) = on ? (1u << pin) : (1u << (pin + 16));  /* BSHR */
        return 0;
    }
    int ch32_delay_ms(int ms) {
        for (volatile uint32_t i = 0; i < (uint32_t)ms * 12000u; i++) { }
        return 0;
    }
  C
  ffi_func :ch32_led_init,  [:int], :int
  ffi_func :ch32_led_write, [:int, :int], :int
  ffi_func :ch32_delay_ms,  [:int], :int
end

CH32.ch32_led_init(0)
10.times do
  CH32.ch32_led_write(0, 1)
  CH32.ch32_delay_ms(250)
  CH32.ch32_led_write(0, 0)
  CH32.ch32_delay_ms(250)
end
