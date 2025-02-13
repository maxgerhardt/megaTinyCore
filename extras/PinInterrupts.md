# Pin interrupts
While the usual `attachInterrupt()` functionality is provided by megaTinyCore (and works on every pin, with every type of trigger), these will take longer to run, and use more flash, than an equivalent interrupt implemented manually (due to the need to check for all 8 pins - while a manually implemented scheme would know that only the pins configured to generate interrupts need to be checked; that takes both time and flash space). Additionally, there are common use cases (for example, reading rotary encoders, particularly more than one) where each pin being handled separately prevents convenient shortcuts from being taken. Worst of all, attaching any pin interrupt through the API causes the core to glom onto EVERY pin change interrupt. I need to fix this, or improve it, or something, but it's a very tricky problem!

It is of course even more of a problem on the Dx-series parts any call to attachInterrupt will block off all 56 interrupts, instead of just 22 at most.

See also: [InterruptVectorNames.md](InterruptVectorNames.md)

For these reasons, it is usually desirable and often necessary to manually implement a pin interrupt instead of using `attachInterrupt();`

## The big attachInterrupt() issue
If your sketch or any library it included calls attachInterrupt(), it takes control of all pin interrupt vectors! Any use of `ISR()` to manually define one of those vectors will fail to compile. Do not use attachInterrupt unless you absolutely have to. And if you do, that's all you can use. Interrupts that fire through attachInterrupt have over 40 clock cycles more overhead compared to a manually defined interrupt!

The most problematic library that uses attachInterrupt is SoftwareSerial (simply because it is most widely used - also it's awful in many ways).

Note that this means that any library that needs to directly define a pin interrupt (for example, to minimize code size or meet response time requirements) is incompatible with any library that uses attachInterrupt, and vice versa. This is an unfortunate consequence of the design decisions made when the attachInterrupt API was written by Arduino. Short of a time machine, there is no hope to change this other than adapting one of them to use the same method as the other one does to create pin interrupts.

**The methods described below are for manually implementing performant flash-efficient pin interrupts that do not use attachInterrupt(), which is fully is covered by the official Arduino reference** except that they don't talk about the response time or the flash usage compared to doing it yourself.

## Manually implementing pin interrupts
The system for interrupts on tinyAVR 0/1-series parts is different from, and vastly more powerful than that of the classic AVR parts. Unlike classic AVR parts, there are no special interrupt pins (INT0, INT1, etc.) - instead, all pins can be configured to generate an interrupt on change, rising, falling or LOW level. While all pins on a port are handled by the same ISR (like classic AVR PCINT's), the pin that triggered the interrupt is recorded in the INTFLAGS register, making it easy to determine which pin triggered the interrupt.

### Enabling the interrupt
The pin interrupt control is handled by the PORTx.PINnCTRL register.
Bits 0-2 control interrupt and sense behavior:

* 000 = no interrupt, normal operation
* 001 = interrupt on change
* 010 = interrupt on rising
* 011 = interrupt on falling
* 100 = digital input buffer disabled entirely (equivalent of DIDR register on classic AVRs that have it)
* 101 = interrupt on LOW level - fires continuously as long as low level is held. This is quite common in the world at large; in many consumer electronics, holding a button has no effect until you release it - this is the simplest implementation that leads to this behavior (though some debounce algorithms do too)

Bit 3 controls the pullup.

Bit 3 is set when `pinMode()` is used to set the pin to INPUT_PULLUP. When manually writing the PINnCTRL registers, be sure to either use bitwise operators to preserve this bit, or set it to the correct value.

### "Fully Asynchronous" pins
Pins that are pin number 2 or 6 within a port are "fully asynchronous" - these are marked on the included pinout charts. These pins provide more sensitive detection of interrupts - on all other pins, an interrupt condition must persist for at least one system clock to trigger the interrupt; on these pins, even shorter pulses can trigger it. Additionally, these pins can wake the system from a sleep mode where the main clock is stopped on a rinsing or falling edge, instead of low level or any change. However, the length of a pulse that triggers an interrupt on these pins is so short that they can be significantly more sensitive to noise than other pins.

### The ISR
Each port has one interrupt vector; their names are:

* PORTA_PORT_vect
* PORTB_PORT_vect
* PORTC_PORT_vect

When the interrupt condition occurs, the bit int PORTx.INTFLAGS corresponding to the interrupt will be set. If multiple pins in a port are used for interrupts corresponding to different things, you can use this to determine which pin triggered the interrupt. **YOU MUST CLEAR THIS BIT WITHIN THE ISR** - the interrupt will continue to be generated as long as the flag is set, so if you do not unset it, the ISR will run continuously after it was triggered once. To clear the bit, write a 1 to it; writing 255 to it will clear all of them.

### A basic example for the x16/x06
This code demonstrates using pin interrupts - it should run on any part with more than 8 pins supported by this core.
```cpp
unsigned long previousMillis;
byte ledState;
volatile byte interrupt1 = 0; // changed in ISR, so must be volatile or compiker will optimize it away.
volatile byte interrupt2 = 0;
volatile byte interrupt3 = 0;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(12,INPUT_PULLUP); //PC2
  pinMode(14,INPUT_PULLUP); //PA1
  pinMode(15,INPUT_PULLUP); //PA2
  pinMode(9,INPUT_PULLUP); //PB0
  PORTC.PIN2CTRL=0b00001101; //PULLUPEN=1, ISC=5 trigger low level
  PORTA.PIN1CTRL=0b00001010; //PULLUPEN=1, ISC=2 trigger rising
  PORTA.PIN2CTRL|=0x02; //ISC=2 trigger rising - uses |= so current value of PULLUPEN is preserved
  PORTB.PIN0CTRL=0b00001001; //PULLUPEN=1, ISC=1 trigger both
  Serial.begin(115200);
  delay(10);
  Serial.println("Startup");

}

void loop() {
  if (interrupt1){
    interrupt1=0;
    Serial.println("I1 fired");
  }
  if (interrupt2){
    interrupt2=0;
    Serial.println("I2 fired");
  }
  if (interrupt3){
    interrupt3=0;
    Serial.println("I3 fired");
  }
  //BlinkWithoutDelay, just so you can confirm that the sketch continues to run.
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= 1000) {
    previousMillis = currentMillis;
    if (ledState == LOW) {
      ledState = HIGH;
    } else {
      ledState = LOW;
    }
    digitalWrite(LED_BUILTIN, ledState);
  }
}

ISR(PORTA_PORT_vect) {
  byte flags=PORTA.INTFLAGS;
  PORTA.INTFLAGS=flags; //clear flags
  if (flags&0x02) {
    interrupt1=1;
  }
  if (flags&0x04) {
    interrupt2=1;
  }
}

ISR(PORTB_PORT_vect) {
  PORTB.INTFLAGS=1; //we know only PB0 has an interrupt, so that's the only flag that could be set.
  interrupt3=1;
}

ISR(PORTC_PORT_vect) {
  _PROTECTED_WRITE(RSTCTRL.SWRR,1); //make this pin into an ersatz reset pin.
}
```

### Synchronous and Asynchronous pins
Certain pins (pin 2 and 6 in each port) are "fully asynchronous" - These pins have several special properties:
* They can be triggered by conditions which last less than one processor cycle.
* They can wake the system from sleep on change, rising, falling or level interrupt. Other pins can only wake on change or level interrupt.
* There is no "dead time" between successive interrupts (other pins have a 3 clock cycle "dead time" between successive interrupts)

In the example above, note that the interrupts on pin 14 and 15 (PA1 and PA2) are configured identically. However, if the part was put to sleep, only the one on pin 15/PA2 would be able to wake the part, as they trigger on rising edge, and only PA2 is a fully asynchronous pin.
