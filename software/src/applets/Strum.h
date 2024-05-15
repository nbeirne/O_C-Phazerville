// Copyright (c) 2021, Bryan Head
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "HSicons.h"
#include "HemisphereApplet.h"

class Strum : public HemisphereApplet {
public:
  const char *applet_name() { return "Strum"; }

  void Start() { 
    qselect = io_offset;
    //QuantizerConfigure(0, 6); // Ionian scale
  }

  void Controller() {

    // TODO:
    // - Decide on repeated trigger behavior; options:
    //   - Reset countdown, advance index (resetting if at end)
    //   - Reset countdown, don't advance index unless at end
    //   - Just advance index (allowing repeated notes)
    //   - Start another, overlapping strum
    bool index_out_of_bounds = index < 0 || index >= length;

    if (EndOfADCLag(0)) {
      countdown = 0;
      inc = 1;
      if (index_out_of_bounds)
        index = 0;
    }
    if (EndOfADCLag(1)) {
      countdown = 0;
      inc = -1;
      if (index_out_of_bounds)
        index = length - 1;
    }

    spacing_mod = spacing;
    Modulate(spacing_mod, 1, HEM_BURST_SPACING_MIN, HEM_BURST_SPACING_MAX);

    if (countdown <= 0 && !index_out_of_bounds && inc != 0) {
      int raw_pitch = In(0);
      HS::Quantize(qselect, raw_pitch);
      int note_num = HS::GetLatestNoteNumber(qselect);
      int pitch = HS::QuantizerLookup(qselect, note_num + intervals[index]);
      disp = note_num;
      Out(0, pitch);
      ClockOut(1);

      last_note_dur = 17 * spacing;
      Modulate(last_note_dur, 1, 17 * HEM_BURST_SPACING_MIN, 17 * HEM_BURST_SPACING_MAX);

      countdown += last_note_dur;
      index += inc;
    } else if (countdown > 0) {
      countdown -= 1;
    }
    ForEachChannel(ch) {
      if (Clock(ch))
        StartADCLag(ch);
    }
  }

  void View() {
    gfxPrint(8, 15, OC::scale_names_short[HS::GetScale(qselect)]);
    gfxPrint(42, 15, OC::Strings::note_names_unpadded[HS::GetRootNote(qselect)]);
    if (cursor == QUANT)
      gfxCursor(7, 23, 48);

    if (show_encoder) {
      gfxPrint(8 + pad(100, spacing), 25, spacing);
      --show_encoder;
    } else {
      gfxPrint(8 + pad(100, spacing_mod), 25, spacing_mod);
      if (spacing_mod != spacing) gfxIcon(1, 23, CV_ICON);
    }
    gfxPrint(28, 25, "ms");
    if (cursor == SPACING)
      gfxCursor(1, 33, 27);

    int num_notes = OC::Scales::GetScale(HS::GetScale(qselect)).num_notes;
    const int cursor_width = 9;
    const int col_width = cursor_width + 2;
    const int top = 35;
    const int num_top = top + 10;
    for (int i = 0; i < length; i++) {
      int col = i;
      int offset = intervals[i] % num_notes;
      int octave = intervals[i] / num_notes;
      if (offset < 0) {
        offset += num_notes;
        octave -= 1;
      }
      const uint8_t *octave_mark = octave < 0
        ? DOWN_ARROWS_BTT + (-octave - 1) * 6
        : UP_ARROWS_BTT + (octave - 1) * 6;
      int disp_offset = offset + 1;
      gfxBitmap(col * col_width, num_top, 8, TEENS_8X8 + disp_offset * 8);

      if (octave != 0)
        gfxBitmap(col * col_width + 1, top + 2, 6, octave_mark);
      if (cursor == INTERVAL_START + i) {
        gfxCursor(col * col_width, num_top + 8, cursor_width);
      }

      int quant_note = disp + (intervals[i] % num_notes);
      int32_t pitch = QuantizerLookup(qselect, quant_note);
      int semitone = (MIDIQuantizer::NoteNumber(pitch) + GetRootNote(qselect)) % 12;
      gfxBitmap(col_width * i, 55, 8, NOTE_NAMES + semitone * 8);

      // TODO: Flip indicator on reverse strums, though tbh it looks fine as is
      if (index - inc == i && inc != 0) {
        int c = inc < 0 ? countdown : last_note_dur - countdown;
        int p = Proportion(c, last_note_dur, col_width);
        int x = col * col_width + p;
        CONSTRAIN(x, 0, 63);
        int w = col_width - p;
        CONSTRAIN(w, 0, 63 - x);
        int y = top;
        gfxInvert(x, y, w, 28);
      }
    }
    if (cursor == LENGTH) {
      gfxCursor(0, num_top + 8, min(6 * col_width, 63));
    }
  }

  void OnButtonPress() {
    if (cursor == QUANT)
      HS::QuantizerEdit(qselect);
    else
      isEditing = !isEditing;
  }

  void OnEncoderMove(int direction) {
    if (!isEditing) {
      MoveCursor(cursor, direction, INTERVAL_START + length - 1);
      return;
    }
    switch (cursor) {
    /* handled in Popup editor
    case SCALE:
      NudgeScale(0, direction);
      break;
    case ROOT:
      SetRootNote(0, GetRootNote(qselect) + direction);
      break;
    // TODO: modify qselect instead
    */
    case SPACING:
      spacing = constrain(spacing + direction, HEM_BURST_SPACING_MIN,
                          HEM_BURST_SPACING_MAX);
      show_encoder = HEMISPHERE_PULSE_ANIMATION_TIME;
      break;
    case LENGTH:
      length = constrain(length + direction, 1, MAX_CHORD_LENGTH);
      break;
    default:
      int ix = cursor - INTERVAL_START;
      intervals[ix] =
          constrain(intervals[ix] + direction, MIN_INTERVAL, MAX_INTERVAL);
    }
  }

  uint64_t OnDataRequest() {
    uint64_t data = 0;
    Pack(data, PackLocation{0, 8}, HS::GetScale(qselect));
    Pack(data, PackLocation{8, 4}, HS::GetRootNote(qselect));
    Pack(data, PackLocation{12, 9}, spacing);
    Pack(data, PackLocation{21, 4}, length);
    for (size_t i = 0; i < MAX_CHORD_LENGTH; i++) {
      Pack(data, PackLocation{25 + i * 6, 6}, intervals[i] - MIN_INTERVAL);
    }
    return data;
  }

  void OnDataReceive(uint64_t data) {
    HS::QuantizerConfigure(qselect, Unpack(data, PackLocation{0, 8}));
    HS::SetRootNote(qselect, Unpack(data, PackLocation{8, 4}));
    spacing = Unpack(data, PackLocation{12, 9});
    length = Unpack(data, PackLocation{21, 4});
    CONSTRAIN(spacing, HEM_BURST_SPACING_MIN, HEM_BURST_SPACING_MAX);
    CONSTRAIN(length, 1, MAX_CHORD_LENGTH);

    for (size_t i = 0; i < MAX_CHORD_LENGTH; i++) {
      intervals[i] = Unpack(data, PackLocation{25 + i * 6, 6}) + MIN_INTERVAL;
    }
  }

protected:
  void SetHelp() {
    //                               "------------------" <-- Size Guide
    help[HEMISPHERE_HELP_DIGITALS] = "Strum Up,  Down";
    help[HEMISPHERE_HELP_CVS]      = "Root    ,  Spacing";
    help[HEMISPHERE_HELP_OUTS]     = "Pitch   ,  Trig";
    help[HEMISPHERE_HELP_ENCODER]  = "";
  }

private:
  static const int MAX_CHORD_LENGTH = 6;
  static const int MIN_INTERVAL = -12;
  static const int MAX_INTERVAL = 48;
  enum Params { QUANT, SPACING, LENGTH, INTERVAL_START };

  int8_t intervals[MAX_CHORD_LENGTH] = {0, 4, 7, 9, 11, 14};
  int length = 6;
  int spacing = HEM_BURST_SPACING_MIN;
  int spacing_mod = HEM_BURST_SPACING_MIN; // for display
  int last_note_dur;

  int index = 0;
  int inc = 0;
  int countdown = 0;
  int show_encoder = 0;

  int8_t qselect = 0;

  int cursor = 0;

  int disp = 64;

};

