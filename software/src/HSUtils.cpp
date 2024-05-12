#include <Arduino.h>
#include "OC_core.h"
#include "HemisphereApplet.h"
#include "HSUtils.h"

namespace HS {

  uint32_t popup_tick; // for button feedback
  PopupType popup_type = MENU_POPUP;
  uint8_t qview = 0; // which quantizer's setting is shown in popup
  bool q_edit = false; // flag to edit current quantizer

  braids::Quantizer quantizer[DAC_CHANNEL_LAST]; // global shared quantizers
  int quant_scale[DAC_CHANNEL_LAST];
  int8_t root_note[DAC_CHANNEL_LAST];
  int8_t q_octave[DAC_CHANNEL_LAST];

  int octave_max = 5;

  int select_mode = -1;
  bool cursor_wrap = 0;
  bool auto_save_enabled = false;
#ifdef ARDUINO_TEENSY41
  int trigger_mapping[] = { 1, 2, 3, 4, 1, 2, 3, 4 };
  int cvmapping[] = { 5, 6, 7, 8, 0, 0, 0, 0 };
#else
  int trigger_mapping[] = { 1, 2, 3, 4 };
  int cvmapping[] = { 1, 2, 3, 4 };
#endif
  uint8_t trig_length = 10; // in ms, multiplier for HEMISPHERE_CLOCK_TICKS
  uint8_t screensaver_mode = 2; // 0 = blank, 1 = Meters, 2 = Zaps

  // --- Quantizer helpers
  int GetLatestNoteNumber(int ch) {
    return quantizer[ch].GetLatestNoteNumber();
  }
  int Quantize(int ch, int cv, int root, int transpose) {
    if (root == 0) root = (root_note[ch] << 7);
    return quantizer[ch].Process(cv, root, transpose) + (q_octave[ch] * 12 << 7);
  }
  int QuantizerLookup(int ch, int note) {
    return quantizer[ch].Lookup(note) + (root_note[ch] << 7) + (q_octave[ch] * 12 << 7);
  }
  void QuantizerConfigure(int ch, int scale, uint16_t mask) {
    CONSTRAIN(scale, 0, OC::Scales::NUM_SCALES);
    quant_scale[ch] = scale;
    quantizer[ch].Configure(OC::Scales::GetScale(scale), mask);
  }
  int GetScale(int ch) {
    return quant_scale[ch];
  }
  int GetRootNote(int ch) {
    return root_note[ch];
  }
  int SetRootNote(int ch, int root) {
    CONSTRAIN(root, 0, 11);
    return (root_note[ch] = root);
  }
  void NudgeRootNote(int ch, int dir) {
    int8_t &r = root_note[ch];
    r += dir;
    if (r > 11 && q_octave[ch] < 3) {
      ++q_octave[ch];
      r -= 12;
    }
    if (r < 0 && q_octave[ch] > -3) {
      --q_octave[ch];
      r += 12;
    }
    CONSTRAIN(r, 0, 11);
  }
  void NudgeScale(int ch, int dir) {
    const int max = OC::Scales::NUM_SCALES;
    int &s = quant_scale[ch];

    s+= dir;
    if (s >= max) s = 0;
    if (s < 0) s = max - 1;
    QuantizerConfigure(ch, s);
  }
  void QuantizerEdit(int ch) {
    qview = ch;
    q_edit = true;
  }

} // namespace HS

/* Proportion method using simfloat, useful for calculating scaled values given
 * a fractional value.
 *
 * Solves this:  numerator        ???
 *              ----------- = -----------
 *              denominator       max
 *
 * For example, to convert a parameter with a range of 1 to 100 into value scaled
 * to HEMISPHERE_MAX_CV, to be sent to the DAC:
 *
 * Out(ch, Proportion(value, 100, HEMISPHERE_MAX_CV));
 *
 */
int Proportion(int numerator, int denominator, int max_value) {
    simfloat proportion = int2simfloat((int32_t)numerator) / (int32_t)denominator;
    int scaled = simfloat2int(proportion * max_value);
    return scaled >= 0 ? scaled : scaled + 1;
}

/* Proportion CV values into pixels for display purposes.
 *
 * Solves this:     cv_value           ???
 *              ----------------- = ----------
 *              HEMISPHERE_MAX_CV   max_pixels
 */
int ProportionCV(int cv_value, int max_pixels) {
    // TODO: variable scaling for VOR?
    int prop = constrain(Proportion(cv_value, HEMISPHERE_MAX_INPUT_CV, max_pixels), -max_pixels, max_pixels);
    return prop;
}

/* Add value to a 64-bit storage unit at the specified location */
void Pack(uint64_t &data, PackLocation p, uint64_t value) {
    data |= (value << p.location);
}

/* Retrieve value from a 64-bit storage unit at the specified location and of the specified bit size */
int Unpack(const uint64_t &data, PackLocation p) {
    uint64_t mask = 1;
    for (size_t i = 1; i < p.size; i++) mask |= (0x01 << i);
    return (data >> p.location) & mask;
}

//////////////// Hemisphere-like graphics methods for easy porting
////////////////////////////////////////////////////////////////////////////////
void gfxPos(int x, int y) {
    graphics.setPrintPos(x, y);
}

void gfxPrint(int x, int y, const char *str) {
    graphics.setPrintPos(x, y);
    graphics.print(str);
}

void gfxPrint(int x, int y, int num) {
    graphics.setPrintPos(x, y);
    graphics.print(num);
}

void gfxPrint(const char *str) {
    graphics.print(str);
}

void gfxPrint(int num) {
    graphics.print(num);
}

void gfxPrint(int x_adv, int num) { // Print number with character padding
    for (int c = 0; c < (x_adv / 6); c++) gfxPrint(" ");
    gfxPrint(num);
}

/* Convert CV value to voltage level and print  to two decimal places */
void gfxPrintVoltage(int cv) {
    int v = (cv * 100) / (12 << 7);
    bool neg = v < 0 ? 1 : 0;
    if (v < 0) v = -v;
    int wv = v / 100; // whole volts
    int dv = v - (wv * 100); // decimal
    gfxPrint(neg ? "-" : "+");
    gfxPrint(wv);
    gfxPrint(".");
    if (dv < 10) gfxPrint("0");
    gfxPrint(dv);
    gfxPrint("V");
}

void gfxPixel(int x, int y) {
    graphics.setPixel(x, y);
}

void gfxFrame(int x, int y, int w, int h) {
    graphics.drawFrame(x, y, w, h);
}

void gfxRect(int x, int y, int w, int h) {
    graphics.drawRect(x, y, w, h);
}

void gfxInvert(int x, int y, int w, int h) {
    graphics.invertRect(x, y, w, h);
}

void gfxLine(int x, int y, int x2, int y2) {
    graphics.drawLine(x, y, x2, y2);
}

void gfxDottedLine(int x, int y, int x2, int y2, uint8_t p) {
#ifdef HS_GFX_MOD
    graphics.drawLine(x, y, x2, y2, p);
#else
    graphics.drawLine(x, y, x2, y2);
#endif
}

void gfxCircle(int x, int y, int r) {
    graphics.drawCircle(x, y, r);
}

void gfxBitmap(int x, int y, int w, const uint8_t *data) {
    graphics.drawBitmap8(x, y, w, data);
}

// Like gfxBitmap, but always 8x8
void gfxIcon(int x, int y, const uint8_t *data) {
    gfxBitmap(x, y, 8, data);
}

