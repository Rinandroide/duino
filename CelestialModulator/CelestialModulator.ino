/*
 * DU-INO CELESTIAL MODULATOR v1.0 "Super-Module"
 * 
 * Perpetual 4-Body Gravitational LFO & Orbital Rhythm Generator
 *
 * Coded by Rino Petrozziello [ www.rinopetrozziello.com ]
 *
 * JACK    FUNCTION
 * ----    --------
 * GT1 O - Planet 1 Orbital Clock/Trig Out -> Drum / Envelope Trig 1
 * GT2 O - Planet 2 Orbital Clock/Trig Out -> Drum / Envelope Trig 2
 * GT3 I - Big Bang Trigger (Reset) -> Jack GT3 oppure TASTO 1 SUL MODULO
 * GT4 I - Solar Storm (Caos In)   -> Jack GT4 oppure TASTO 2 SUL MODULO
 * CO1 O - Planet 1 Orbit LFO (0-5V)
 * CO2 O - Planet 2 Orbit LFO (0-5V)
 * CO3 O - Planet 3 Orbit LFO (0-5V)
 * CO4 O - Planet 4 Orbit LFO (0-5V)
 * 
 * SWITCH CONFIGURATION
 * --------------------
 * SG2    [^][^]    SG1
 * SG4    [_][_]    SG3
 * SC2    [^][^]    SC1
 * SC4    [^][^]    SC3
 *
 * [^] = Switch UP   (Output / Normalling Disengaged)
 * [_] = Switch DOWN (Input  / Normalling Engaged)
 */

#include <du-ino_function.h>
#include <du-ino_widgets.h>
#include <du-ino_save.h>
#include <du-ino_sh1106.h>
#include <du-ino_utils.h>
#include <avr/pgmspace.h>

#define NUM_PLANETS 4
#define TRAIL_LEN 6

const unsigned char epd_bitmap_logo_rino[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x06, 0x00, 0x06, 0x00, 0x07, 0xe0, 0x07, 0xe0, 
  0x07, 0x80, 0x07, 0x00, 0x07, 0x00, 0x06, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

void draw_custom_bitmap(int x, int y, int w, int h, const uint8_t *bitmap) {
  int bytes_per_row = (w + 7) / 8;
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      int byte_idx = (j * bytes_per_row) + (i / 8);
      int bit_idx = 7 - (i % 8);
      uint8_t b = pgm_read_byte(&bitmap[byte_idx]);
      if ((b >> bit_idx) & 1) {
        Display.draw_pixel(x + i, y + j, DUINO_SH1106::White);
      }
    }
  }
}

void speed_scroll(int delta);
void gravity_scroll(int delta);
void entropy_scroll(int delta);
void bigbang_isr();

class CelestialModulator : public DUINO_Function
{
public:
  CelestialModulator() : DUINO_Function(0b11001100) { }

  struct Planet {
    float x, y;
    float vx, vy;
    float last_y;
    int8_t historyX[TRAIL_LEN];
    int8_t historyY[TRAIL_LEN];
  };

  struct ParameterValues {
    int8_t speed;    // 1 - 20 (Velocità simulazione)
    int8_t gravity;  // 1 - 20 (Massa stella centrale)
    int8_t entropy;  // 0 - 20 (Vento solare / Caos)
  };

  DUINO_WidgetContainer<4> * container_;
  DUINO_SaveWidget<ParameterValues> * widget_save_;
  DUINO_DisplayWidget * widget_speed_;
  DUINO_DisplayWidget * widget_gravity_;
  DUINO_DisplayWidget * widget_entropy_;

  Planet planets[NUM_PLANETS];
  unsigned long last_frame_time_ = 0;
  unsigned long gt1_trig_timer_ = 0;
  unsigned long gt2_trig_timer_ = 0;
  bool gt1_active_ = false;
  bool gt2_active_ = false;
  volatile bool reset_requested_ = false;

  virtual void function_setup() {
    container_ = new DUINO_WidgetContainer<4>(DUINO_Widget::DoubleClick, 2);
    widget_save_ = new DUINO_SaveWidget<ParameterValues>(121, 0);
    container_->attach_child(widget_save_, 0);
    widget_speed_ = new DUINO_DisplayWidget(68, 0, 12, 9, DUINO_Widget::Full);
    widget_speed_->attach_scroll_callback(speed_scroll);
    container_->attach_child(widget_speed_, 1);
    widget_gravity_ = new DUINO_DisplayWidget(88, 0, 12, 9, DUINO_Widget::Full);
    widget_gravity_->attach_scroll_callback(gravity_scroll);
    container_->attach_child(widget_gravity_, 2);
    widget_entropy_ = new DUINO_DisplayWidget(108, 0, 12, 9, DUINO_Widget::Full);
    widget_entropy_->attach_scroll_callback(entropy_scroll);
    container_->attach_child(widget_entropy_, 3);
    gt_attach_interrupt(GT3, bigbang_isr, RISING);
    widget_save_->load_params();

    if (widget_save_->params.vals.speed < 1 || widget_save_->params.vals.speed > 20) {
      widget_save_->params.vals.speed = 10;
    }
    if (widget_save_->params.vals.gravity < 1 || widget_save_->params.vals.gravity > 20) {
      widget_save_->params.vals.gravity = 10;
    }
    if (widget_save_->params.vals.entropy < 0 || widget_save_->params.vals.entropy > 20) {
      widget_save_->params.vals.entropy = 2;
    }

    bigBang();

    Display.draw_du_logo_sm(0, 2, DUINO_SH1106::White);
    draw_custom_bitmap(15, -3, 16, 16, epd_bitmap_logo_rino);
    Display.draw_text(32, 2, "GRAV", DUINO_SH1106::White);
    Display.fill_rect(widget_save_->x() + 1, widget_save_->y() + 1, 5, 5, DUINO_SH1106::White);
    Display.draw_char(62, 2, 'S', DUINO_SH1106::White);
    Display.draw_char(82, 2, 'G', DUINO_SH1106::White);
    Display.draw_char(102, 2, 'E', DUINO_SH1106::White);
    update_ui_values();
    widget_setup(container_);
    Display.display();
  }

  virtual void function_loop() {
    widget_loop();

    if (reset_requested_) {
      reset_requested_ = false;
      bigBang();
    }

    unsigned long now = millis();
    if (gt1_active_ && (now - gt1_trig_timer_ >= 10)) {
      gt_out(GT1, false);
      gt1_active_ = false;
    }
    if (gt2_active_ && (now - gt2_trig_timer_ >= 10)) {
      gt_out(GT2, false);
      gt2_active_ = false;
    }

    if (now - last_frame_time_ >= 25) {
      last_frame_time_ = now;
      update_physics();
      render_space();
    }
  }

  void update_physics() {
    float dt = (float)widget_save_->params.vals.speed * 0.016f;
    float gravMass = (float)widget_save_->params.vals.gravity * 0.12f;
    float entropyFactor = (float)widget_save_->params.vals.entropy * 0.0006f;

    if (gt_read(GT4)) {
      entropyFactor *= 8.0f;
    }

    for (int i = 0; i < NUM_PLANETS; i++) {
      Planet &p = planets[i];
      p.last_y = p.y;

      float d2 = p.x * p.x + p.y * p.y;
      if (d2 < 0.45f) d2 = 0.45f;
      float d = sqrt(d2);

      float force = gravMass / d2;
      float fx = -force * (p.x / d);
      float fy = -force * (p.y / d);

      if (entropyFactor > 0.0f) {
        fx += ((float)random(-50, 51) * 0.02f) * entropyFactor;
        fy += ((float)random(-50, 51) * 0.02f) * entropyFactor;
      }

      p.vx += fx * dt;
      p.vy += fy * dt;
      p.x  += p.vx * dt;
      p.y  += p.vy * dt;

      if (d > 7.0f) {
        respawnPlanet(i);
      }

      if (i == 0 && p.last_y < 0.0f && p.y >= 0.0f) {
        gt_out(GT1, true);
        gt1_active_ = true;
        gt1_trig_timer_ = millis();
      }
      if (i == 1 && p.last_y < 0.0f && p.y >= 0.0f) {
        gt_out(GT2, true);
        gt2_active_ = true;
        gt2_trig_timer_ = millis();
      }

      for (int k = TRAIL_LEN - 1; k > 0; k--) {
        p.historyX[k] = p.historyX[k - 1];
        p.historyY[k] = p.historyY[k - 1];
      }
      p.historyX[0] = 64 + (int8_t)(p.x * 15.0f);
      p.historyY[0] = 38 + (int8_t)(p.y * 10.5f);
    }

    cv_out(CO1, clamp<float>(2.5f + (planets[0].y * 0.95f), 0.0f, 5.0f));
    cv_out(CO2, clamp<float>(2.5f + (planets[1].y * 0.95f), 0.0f, 5.0f));
    cv_out(CO3, clamp<float>(2.5f + (planets[2].y * 0.95f), 0.0f, 5.0f));
    cv_out(CO4, clamp<float>(2.5f + (planets[3].y * 0.95f), 0.0f, 5.0f));
  }

  void render_space() {
    Display.fill_rect(0, 10, 128, 54, DUINO_SH1106::Black);
    Display.fill_rect(63, 37, 3, 3, DUINO_SH1106::White);

    for (int i = 0; i < NUM_PLANETS; i++) {
      for (int k = 1; k < TRAIL_LEN; k++) {
        int8_t hx = planets[i].historyX[k];
        int8_t hy = planets[i].historyY[k];
        if (hx >= 0 && hx < 128 && hy >= 10 && hy < 64) {
          Display.draw_pixel(hx, hy, DUINO_SH1106::White);
        }
      }

      int8_t px = 64 + (int8_t)(planets[i].x * 15.0f);
      int8_t py = 38 + (int8_t)(planets[i].y * 10.5f);
      if (px >= 0 && px < 127 && py >= 10 && py < 63) {
        Display.fill_rect(px, py, 2, 2, DUINO_SH1106::White);
      }
    }

    Display.display(0, 127, 1, 7);
  }

  void update_ui_values()
  {
    draw_val(widget_speed_, widget_save_->params.vals.speed);
    draw_val(widget_gravity_, widget_save_->params.vals.gravity);
    draw_val(widget_entropy_, widget_save_->params.vals.entropy);
  }

  void draw_val(DUINO_DisplayWidget* w, int val)
  {
    Display.fill_rect(w->x(), w->y(), 12, 9, DUINO_SH1106::Black);
    Display.draw_char(w->x(), w->y() + 1, '0' + (val / 10), DUINO_SH1106::White);
    Display.draw_char(w->x() + 6, w->y() + 1, '0' + (val % 10), DUINO_SH1106::White);
    if (w->inverted()) Display.fill_rect(w->x(), w->y(), 12, 9, DUINO_SH1106::Inverse);
    w->display();
  }

  void handle_scroll_speed(int delta) {
    if (adjust<int8_t>(widget_save_->params.vals.speed, delta, 1, 20)) {
      widget_save_->mark_changed();
      draw_val(widget_speed_, widget_save_->params.vals.speed);
    }
  }

  void handle_scroll_gravity(int delta) {
    if (adjust<int8_t>(widget_save_->params.vals.gravity, delta, 1, 20)) {
      widget_save_->mark_changed();
      draw_val(widget_gravity_, widget_save_->params.vals.gravity);
    }
  }

  void handle_scroll_entropy(int delta) {
    if (adjust<int8_t>(widget_save_->params.vals.entropy, delta, 0, 20)) {
      widget_save_->mark_changed();
      draw_val(widget_entropy_, widget_save_->params.vals.entropy);
    }
  }

  void respawnPlanet(int i) {
    float angle = (float)random(0, 628) * 0.01f;
    float dist = 1.1f + ((float)i * 0.62f);
    planets[i].x = cos(angle) * dist;
    planets[i].y = sin(angle) * dist;
    planets[i].last_y = planets[i].y;

    float v = sqrt(1.2f / dist);
    planets[i].vx = -sin(angle) * v;
    planets[i].vy = cos(angle) * v;

    for (int k = 0; k < TRAIL_LEN; k++) {
      planets[i].historyX[k] = 64 + (int8_t)(planets[i].x * 15.0f);
      planets[i].historyY[k] = 38 + (int8_t)(planets[i].y * 10.5f);
    }
  }

  void bigBang() {
    for (int i = 0; i < NUM_PLANETS; i++) {
      respawnPlanet(i);
    }
  }

  void trigger_bigbang_from_isr() {
    reset_requested_ = true;
  }
};

CelestialModulator * function;

void speed_scroll(int delta) { function->handle_scroll_speed(delta); }
void gravity_scroll(int delta) { function->handle_scroll_gravity(delta); }
void entropy_scroll(int delta) { function->handle_scroll_entropy(delta); }
void bigbang_isr() { function->trigger_bigbang_from_isr(); }

void setup() {
  function = new CelestialModulator();
  function->begin();
}

void loop() {
  function->function_loop();
}
