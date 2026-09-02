/*
 * DU-INO EUCLID-DRUM v1.0 "Super Module"
 * Dual Euclidean Rhythm & Random CV Generator
 * 
 * Coded by Rino Petrozziello [ www.rinopetrozziello.com ]
 *
 * JACK    FUNCTION
 * ----    --------
 * GT1 O - Trigger Out Channel 1 -> Drum Trig 1
 * GT2 O - Trigger Out Channel 2 -> Drum Trig 2
 * GT3 I - Clock In (External Clock)
 * GT4 I - Reset In
 * CO1   - Random Step CV Out 1 (0-5V)
 * CO2   - Random Step CV Out 2 (0-5V)
 * CI1   - [Unused]
 * CI2   - [Unused]
 * 
 * SWITCH CONFIGURATION
 * --------------------
 * SG2    [^][^]    SG1
 * SG4    [_][_]    SG3
 * SC2    [^][^]    SC1
 * SC4    [_][_]    SC3
 *
 * [^] = Switch UP   (Output / Normalling Disengaged)
 * [_] = Switch DOWN (Input  / Normalling Engaged)
 */

#include <du-ino_function.h>
#include <du-ino_widgets.h>
#include <du-ino_save.h>
#include <du-ino_clock.h>
#include <du-ino_utils.h>
#include <avr/pgmspace.h>

#define CLOCK_BPM_MAX 300
#define STEPS_MIN 1
#define STEPS_MAX 16 
#define HITS_MIN 0

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

uint16_t calculate_euclidean_bitmap(uint8_t steps, uint8_t hits, int8_t rot) {
  if (steps == 0 || hits == 0) return 0;
  if (hits >= steps) return (0xFFFF >> (16 - steps)); 
  
  uint16_t pattern = 0;
  for (uint8_t i = 0; i < hits; i++) {
     uint16_t num = (uint16_t)i * steps;
     uint8_t base_pos = num / hits;
     uint8_t pos = (base_pos + rot) % steps;
     pattern |= (1 << pos);
  }
  return pattern;
}

void clock_ext_isr();
void reset_isr();
void clock_callback();
void external_callback();
void bpm_scroll(int delta);
void steps1_scroll(int delta);
void hits1_scroll(int delta);
void rot1_scroll(int delta);
void steps2_scroll(int delta);
void hits2_scroll(int delta);
void rot2_scroll(int delta);

class DU_EUCLID_Function : public DUINO_Function
{
public:
  DU_EUCLID_Function() : DUINO_Function(0b11001100) { }

  virtual void function_setup()
  {
    container_outer_ = new DUINO_WidgetContainer<8>(DUINO_Widget::DoubleClick, 2);
    
    widget_save_ = new DUINO_SaveWidget<ParameterValues>(121, 0);
    container_outer_->attach_child(widget_save_, 0);

    widget_bpm_ = new DUINO_DisplayWidget(80, 0, 20, 9, DUINO_Widget::Full);
    widget_bpm_->attach_scroll_callback(bpm_scroll);
    container_outer_->attach_child(widget_bpm_, 1);

    widget_steps1_ = new DUINO_DisplayWidget(0, 25, 12, 9, DUINO_Widget::Full);
    widget_steps1_->attach_scroll_callback(steps1_scroll);
    container_outer_->attach_child(widget_steps1_, 2);

    widget_rot1_ = new DUINO_DisplayWidget(28, 25, 12, 9, DUINO_Widget::Full);
    widget_rot1_->attach_scroll_callback(rot1_scroll);
    container_outer_->attach_child(widget_rot1_, 3);

    widget_hits1_ = new DUINO_DisplayWidget(110, 25, 12, 9, DUINO_Widget::Full);
    widget_hits1_->attach_scroll_callback(hits1_scroll);
    container_outer_->attach_child(widget_hits1_, 4);

    widget_steps2_ = new DUINO_DisplayWidget(0, 50, 12, 9, DUINO_Widget::Full);
    widget_steps2_->attach_scroll_callback(steps2_scroll);
    container_outer_->attach_child(widget_steps2_, 5);

    widget_rot2_ = new DUINO_DisplayWidget(28, 50, 12, 9, DUINO_Widget::Full);
    widget_rot2_->attach_scroll_callback(rot2_scroll);
    container_outer_->attach_child(widget_rot2_, 6);

    widget_hits2_ = new DUINO_DisplayWidget(110, 50, 12, 9, DUINO_Widget::Full);
    widget_hits2_->attach_scroll_callback(hits2_scroll);
    container_outer_->attach_child(widget_hits2_, 7);

    Clock.begin();
    Clock.attach_clock_callback(clock_callback);
    Clock.attach_external_callback(external_callback);

    gt_attach_interrupt(GT3, clock_ext_isr, CHANGE);
    gt_attach_interrupt(GT4, reset_isr, RISING);

    widget_save_->load_params();

    if (widget_save_->params.vals.steps1 < STEPS_MIN || widget_save_->params.vals.steps1 > STEPS_MAX) {
        widget_save_->params.vals.steps1 = 16;
        widget_save_->params.vals.hits1 = 4;
        widget_save_->params.vals.rot1 = 0;
    }
    if (widget_save_->params.vals.steps2 < STEPS_MIN || widget_save_->params.vals.steps2 > STEPS_MAX) {
        widget_save_->params.vals.steps2 = 16;
        widget_save_->params.vals.hits2 = 4;
        widget_save_->params.vals.rot2 = 0;
    }

    widget_save_->params.vals.bpm = clamp<int16_t>(widget_save_->params.vals.bpm, 0, CLOCK_BPM_MAX);
    if (widget_save_->params.vals.bpm > 0) {
      Clock.set_bpm(widget_save_->params.vals.bpm);
    } else {
      Clock.set_external();
    }

    widget_save_->params.vals.hits1 = clamp<int8_t>(widget_save_->params.vals.hits1, HITS_MIN, widget_save_->params.vals.steps1);
    widget_save_->params.vals.rot1 = clamp<int8_t>(widget_save_->params.vals.rot1, 0, widget_save_->params.vals.steps1 - 1);
    widget_save_->params.vals.hits2 = clamp<int8_t>(widget_save_->params.vals.hits2, HITS_MIN, widget_save_->params.vals.steps2);
    widget_save_->params.vals.rot2 = clamp<int8_t>(widget_save_->params.vals.rot2, 0, widget_save_->params.vals.steps2 - 1);

    recalc_patterns();

    cur_step1_ = 0;
    cur_step2_ = 0;
    needs_display_update_ = true;

    Display.draw_du_logo_sm(0, 2, DUINO_SH1106::White);
    draw_custom_bitmap(15, -3, 16, 16, epd_bitmap_logo_rino);
    Display.draw_text(32, 2, "Euclid", DUINO_SH1106::White);
    Display.fill_rect(widget_save_->x() + 1, widget_save_->y() + 1, 5, 5, DUINO_SH1106::White);
    Display.draw_char(13, 26, 'S', DUINO_SH1106::White);
    Display.draw_char(42, 26, 'R', DUINO_SH1106::White);
    Display.draw_char(98, 26, 'H', DUINO_SH1106::White);
    Display.draw_char(13, 51, 'S', DUINO_SH1106::White);
    Display.draw_char(42, 51, 'R', DUINO_SH1106::White);
    Display.draw_char(98, 51, 'H', DUINO_SH1106::White);

    update_ui_values();
    widget_setup(container_outer_);
    Display.display();
  }

  virtual void function_loop()
  {
    widget_loop();

    if (needs_display_update_) {
      needs_display_update_ = false;
      draw_circles();
    }
  }

  void recalc_patterns() {
    pattern1_cache_ = calculate_euclidean_bitmap(widget_save_->params.vals.steps1, widget_save_->params.vals.hits1, widget_save_->params.vals.rot1);
    pattern2_cache_ = calculate_euclidean_bitmap(widget_save_->params.vals.steps2, widget_save_->params.vals.hits2, widget_save_->params.vals.rot2);
  }

  void on_clock_tick() {
    if (Clock.state()) { 

      bool trig1 = (pattern1_cache_ >> cur_step1_) & 1;
      gt_out(GT1, trig1);
      float rnd1 = (float)random(0, 1001) * 0.005f; 
      cv_out(CO1, rnd1);
      cur_step1_++;
      if (cur_step1_ >= widget_save_->params.vals.steps1) cur_step1_ = 0;

      bool trig2 = (pattern2_cache_ >> cur_step2_) & 1;
      gt_out(GT2, trig2);
      float rnd2 = (float)random(0, 1001) * 0.005f;
      cv_out(CO2, rnd2);
      cur_step2_++;
      if (cur_step2_ >= widget_save_->params.vals.steps2) cur_step2_ = 0;

      needs_display_update_ = true;
    } else {
      gt_out(GT1, false);
      gt_out(GT2, false);
    }
  }

  void on_reset() {
    cur_step1_ = 0;
    cur_step2_ = 0;
    Clock.reset();
    needs_display_update_ = true;
  }

  void on_ext_clock_change() {
    widget_save_->params.vals.bpm = 0; 
    update_ui_values();
  }

  void update_ui_values() {
    Display.fill_rect(widget_bpm_->x(), widget_bpm_->y(), 20, 9, DUINO_SH1106::Black);
    int x = widget_bpm_->x();
    int y = widget_bpm_->y() + 1;
    if(widget_save_->params.vals.bpm == 0) {
       Display.draw_text(x, y, "EXT", DUINO_SH1106::White);
    } else {
       int val = widget_save_->params.vals.bpm;
       Display.draw_char(x, y, '0' + (val/100), DUINO_SH1106::White);
       Display.draw_char(x+6, y, '0' + ((val%100)/10), DUINO_SH1106::White);
       Display.draw_char(x+12, y, '0' + (val%10), DUINO_SH1106::White);
    }
    if(widget_bpm_->inverted()) Display.fill_rect(widget_bpm_->x(), widget_bpm_->y(), 20, 9, DUINO_SH1106::Inverse);
    widget_bpm_->display();
    
    draw_num(widget_steps1_, widget_save_->params.vals.steps1);
    draw_num(widget_rot1_, widget_save_->params.vals.rot1);
    draw_num(widget_hits1_, widget_save_->params.vals.hits1);
    draw_num(widget_steps2_, widget_save_->params.vals.steps2);
    draw_num(widget_rot2_, widget_save_->params.vals.rot2);
    draw_num(widget_hits2_, widget_save_->params.vals.hits2);

    needs_display_update_ = true;
  }

  void draw_num(DUINO_DisplayWidget* w, int val) {
    Display.fill_rect(w->x(), w->y(), 12, 9, DUINO_SH1106::Black);
    Display.draw_char(w->x(), w->y()+1, '0' + (val/10), DUINO_SH1106::White);
    Display.draw_char(w->x()+6, w->y()+1, '0' + (val%10), DUINO_SH1106::White);
    if(w->inverted()) Display.fill_rect(w->x(), w->y(), 12, 9, DUINO_SH1106::Inverse);
    w->display(); 
  }

  void draw_circles() {
    draw_ring(64, 28, 8, widget_save_->params.vals.steps1, cur_step1_, pattern1_cache_);
    draw_ring(64, 52, 8, widget_save_->params.vals.steps2, cur_step2_, pattern2_cache_);
    Display.display(30, 90, 2, 7); 
  }

  void draw_ring(int cx, int cy, int r, int steps, int cur, uint16_t pattern) {
    Display.fill_rect(cx - r - 2, cy - r - 2, (r * 2) + 5, (r * 2) + 5, DUINO_SH1106::Black);
    for(int i = 0; i < steps; i++) {
      float angle = (float)i * (6.2831853f / (float)steps) - 1.5707963f;
      int px = cx + (int)(cos(angle) * r);
      int py = cy + (int)(sin(angle) * r);
      bool is_hit = (pattern >> i) & 1;
      bool is_active = (i == (cur == 0 ? steps - 1 : cur - 1)); 
      if (is_active) {
         Display.fill_rect(px - 1, py - 1, 3, 3, DUINO_SH1106::White);
      } else if (is_hit) {
         Display.draw_pixel(px, py, DUINO_SH1106::White);
      } else {
         if (steps < 10) Display.draw_pixel(px, py, DUINO_SH1106::White); 
      }
    }
  }

  void handle_scroll_bpm(int delta) {
    if(adjust<int16_t>(widget_save_->params.vals.bpm, delta, 0, CLOCK_BPM_MAX)) {
      if(widget_save_->params.vals.bpm > 0) Clock.set_bpm(widget_save_->params.vals.bpm);
      else Clock.set_external();
      widget_save_->mark_changed();
      update_ui_values();
    }
  }

  void handle_scroll_generic(int8_t &val, int delta, int min, int max_limit, bool is_step_change) {
    if(adjust<int8_t>(val, delta, min, max_limit)) {
      widget_save_->mark_changed();
      if(is_step_change) {
        if (widget_save_->params.vals.hits1 > widget_save_->params.vals.steps1) widget_save_->params.vals.hits1 = widget_save_->params.vals.steps1;
        if (widget_save_->params.vals.rot1 >= widget_save_->params.vals.steps1) widget_save_->params.vals.rot1 = widget_save_->params.vals.steps1 - 1;
        if (widget_save_->params.vals.hits2 > widget_save_->params.vals.steps2) widget_save_->params.vals.hits2 = widget_save_->params.vals.steps2;
        if (widget_save_->params.vals.rot2 >= widget_save_->params.vals.steps2) widget_save_->params.vals.rot2 = widget_save_->params.vals.steps2 - 1;
      }
      recalc_patterns();
      update_ui_values();
    }
  }

public:
  struct ParameterValues {
    int16_t bpm;
    int8_t steps1, hits1, rot1;
    int8_t steps2, hits2, rot2;
  };

  DUINO_WidgetContainer<8> * container_outer_;
  DUINO_SaveWidget<ParameterValues> * widget_save_;
  DUINO_DisplayWidget * widget_bpm_;
  DUINO_DisplayWidget * widget_steps1_;
  DUINO_DisplayWidget * widget_rot1_;
  DUINO_DisplayWidget * widget_hits1_;
  DUINO_DisplayWidget * widget_steps2_;
  DUINO_DisplayWidget * widget_rot2_;
  DUINO_DisplayWidget * widget_hits2_;

  volatile uint16_t pattern1_cache_, pattern2_cache_;
  volatile uint8_t cur_step1_, cur_step2_;
  volatile bool needs_display_update_;
};

DU_EUCLID_Function * function;

void clock_ext_isr() { Clock.on_jack(function->gt_read(DUINO_Function::GT3)); }
void reset_isr() { function->on_reset(); }
void clock_callback() { function->on_clock_tick(); }
void external_callback() { function->on_ext_clock_change(); }

void bpm_scroll(int delta) { function->handle_scroll_bpm(delta); }
void steps1_scroll(int delta) { function->handle_scroll_generic(function->widget_save_->params.vals.steps1, delta, STEPS_MIN, STEPS_MAX, true); }
void hits1_scroll(int delta) { function->handle_scroll_generic(function->widget_save_->params.vals.hits1, delta, HITS_MIN, function->widget_save_->params.vals.steps1, false); }
void rot1_scroll(int delta) { function->handle_scroll_generic(function->widget_save_->params.vals.rot1, delta, 0, function->widget_save_->params.vals.steps1 - 1, false); }

void steps2_scroll(int delta) { function->handle_scroll_generic(function->widget_save_->params.vals.steps2, delta, STEPS_MIN, STEPS_MAX, true); }
void hits2_scroll(int delta) { function->handle_scroll_generic(function->widget_save_->params.vals.hits2, delta, HITS_MIN, function->widget_save_->params.vals.steps2, false); }
void rot2_scroll(int delta) { function->handle_scroll_generic(function->widget_save_->params.vals.rot2, delta, 0, function->widget_save_->params.vals.steps2 - 1, false); }

void setup()
{
  function = new DU_EUCLID_Function();
  function->begin();
}

void loop()
{
  function->function_loop();
}
