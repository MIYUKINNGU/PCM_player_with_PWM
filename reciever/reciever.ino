#include <hardware/pwm.h>
#include <hardware/dma.h>
#define SPEEKER 12
#define data_size 317458
#define MACHINE_SPEED 200000000.0
#define PWM_WRAP 4096
#define Playing_rate 44100.0
#define MAX_BUFFER 32768
#define PACKET_SIZE 512
#define SWITCH 16

unsigned long start_time = 0;
uint16_t buffer[MAX_BUFFER];
unsigned int slice_num = 0;
unsigned int apdidx = 0;
unsigned long timeout_counter = 0;
bool isPlaying = false;
int dma_chan;
int got_counter = 0;
bool isPaused = false;
bool doesSwitchTurnOn = false;

void setupPWM() {
  gpio_set_function(SPEEKER, GPIO_FUNC_PWM);
  slice_num = pwm_gpio_to_slice_num(SPEEKER);

  pwm_set_clkdiv(slice_num, MACHINE_SPEED / (PWM_WRAP * Playing_rate));
  pwm_set_wrap(slice_num, PWM_WRAP - 1);
  pwm_set_chan_level(slice_num, PWM_CHAN_A, 0);
  pwm_set_enabled(slice_num, false);
}

void __isr dma_handler() {
  dma_hw->ints0 = 1u << dma_chan;

  // 次のバッファにDMA設定
  dma_channel_set_read_addr(dma_chan, buffer, true);
}

void setup_dma() {
  dma_chan = dma_claim_unused_channel(true);
  dma_channel_config c = dma_channel_get_default_config(dma_chan);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, DREQ_PWM_WRAP0 + slice_num);

  dma_channel_configure(
    dma_chan,
    &c,
    &pwm_hw->slice[slice_num].cc,
    buffer,
    MAX_BUFFER,
    false
  );

  dma_channel_set_irq0_enabled(dma_chan, true);
  irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
  irq_set_enabled(DMA_IRQ_0, true);
}

void reset_buffer() {
  for (size_t i = 0; i < MAX_BUFFER; i++) {
    buffer[i] = 0;
  }
}

void setup() {
  set_sys_clock_khz((unsigned int)MACHINE_SPEED / 1000, true);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(SPEEKER, OUTPUT);
  pinMode(SWITCH, INPUT_PULLUP);
  Serial.begin(115200);

  setupPWM();
  setup_dma();

  timeout_counter = micros();
}

void loop() {
  while (Serial.available() >= 2) {
    digitalWrite(LED_BUILTIN, LOW);
    int low = Serial.read();
    int high = Serial.read();
    uint16_t sample = ((high & 0xFF) << 8) | (low & 0xFF); // little-endian
    sample &= 0x0FFF; // 12-bit
    buffer[apdidx % MAX_BUFFER] = sample;
    got_counter++;
    apdidx++;
    if (apdidx % MAX_BUFFER >= MAX_BUFFER/5 && !isPlaying) {
      isPlaying = true;
      dma_start_channel_mask(1u << dma_chan);
      pwm_set_enabled(slice_num, true);
    }
    timeout_counter = micros();
  }
  if (Serial.available() == 0) {
    digitalWrite(LED_BUILTIN, HIGH);
  }

  int sw = digitalRead(SWITCH);
  if (sw == 1 && !doesSwitchTurnOn) {
    doesSwitchTurnOn = true;
    isPaused = !isPaused;
    if (isPaused) pwm_set_enabled(slice_num, false);
    else pwm_set_enabled(slice_num, true);
  }
  else if (sw == 0) {
    doesSwitchTurnOn = false;
  }

  if (isPaused) {
    timeout_counter = micros();
    return;
  }

  if (micros() - timeout_counter > 500000) {
    isPlaying = false;
    pwm_set_enabled(slice_num, false);
    apdidx = 0;
    got_counter = 0;
    Serial.write('\x12');
    reset_buffer();
  }

  uint32_t idx = (dma_channel_hw_addr(dma_chan)->read_addr - (uint32_t)buffer) / sizeof(uint16_t);
  uint32_t addr = apdidx % MAX_BUFFER;
  uint32_t value = addr - idx;
  if (value < 0) value += MAX_BUFFER;
  if (got_counter >= PACKET_SIZE && value < MAX_BUFFER * 3 / 5) {
    got_counter = 0;
    Serial.write('\x12');
  }
}