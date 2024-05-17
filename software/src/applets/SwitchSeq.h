// Copyright (c) 2020, Jason Justian, Nick Beirne
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Uses the memory space in OC_patterns to sequence. Like a mini sequins.
// 4 sequences are played in parallel. The note values may be configured with sequins or an upcoming sequence editor. CV can manipulate the sequences in a variety of ways.
//
// There are two channels controlled with a single clock (GATE 1). Gate 2 is a reset to makes the internal sequwncer's step equal to 0.
// Each channel has a few different modes, controllable with the encoder.
//
// Modes:
// OCT:  you are in this mode when the sequence arrow is solid. The encoder will rotate through sequences and it will play the chosen sequence, with CV controlling the octave (by 1 or 2 octaves).
// PICK: In this mode CV will pick which sequence is playing. Sampled after each clock.
// RAND: The CV is ignored, and instead a random sequence is chosen at random.
// QUAN: The internal sequences are ignored and the CV will be quanitzed to a global scale. Right now that's c-dorian and only configurable in code.

#include "MiniSeq.h"

#define PICK_MODE -1
#define RAND_MODE -2
#define QUAN_MODE -3

#define OCTAVE_1 (12 << 7)

// 5.5 volts to play well with pressure points
#define PP_MAX_INPUT_CV (HEMISPHERE_MAX_INPUT_CV * 11 / 12)
#define OCTAVE_RANGE 2

class SwitchSeq : public HemisphereApplet
{
public:
    const char *applet_name()
    {
        return "SwitchSeq";
    }

    void Start()
    {
      for (int i = 0; i < 4; ++i) {
        miniseq[i].SetPattern(i);
      }
      Out(0, 0); 
      Out(1, 0);
    }

    void Controller()
    {
        // global clock. Do it before reset.
        if (Clock(0))
        {
            StartADCLag(0);
            Advance();
        }

        // reset
        if (Clock(1))
        {
            StartADCLag(1);
            Reset();
        }

        // only make changes on a clock'd signal.
        if (EndOfADCLag(0) || EndOfADCLag(1))
        {
            ForEachChannel(ch)
            {
                if (mode[ch] == RAND_MODE)
                {
                    cv[ch] = random(0, PP_MAX_INPUT_CV);
                }
                else
                {
                    cv[ch] = In(ch);
                }
                int32_t pitch = ValueForChannel(ch);
                int32_t quantized = Quantize(ch, pitch);
                Out(ch, quantized);
            }
        }
    }

    void View()
    {
        gfxHeader(applet_name());
        // DrawScale();
        DrawMode();
        DrawIndicator();
    }

    void OnButtonPress()
    {
        if (++cursor > 1)
            cursor = 0;
        ResetCursor();
    }

    void OnEncoderMove(int direction)
    {
        mode[cursor] += direction;
        if (mode[cursor] < -3)
            mode[cursor] = -3;
        if (mode[cursor] > 3)
            mode[cursor] = 3;
    }

    uint64_t OnDataRequest()
    {
        uint64_t data = 0;
        Pack(data, PackLocation{0, 8}, mode[0] + 32);
        Pack(data, PackLocation{8, 8}, mode[1] + 32);
        return data;
    }

    void OnDataReceive(uint64_t data)
    {
        mode[0] = Unpack(data, PackLocation{0, 8}) - 32;
        mode[1] = Unpack(data, PackLocation{8, 8}) - 32;
    }

protected:
    // TODO: global scale
    // TODO: help

    void SetHelp()
    {
        //                               "------------------" <-- Size Guide
        help[HEMISPHERE_HELP_DIGITALS] = "1=Clock,2=Reset";
        help[HEMISPHERE_HELP_CVS] = "CV Ch1,Ch2";
        help[HEMISPHERE_HELP_OUTS] = "A=Out1,B=Out1";
        help[HEMISPHERE_HELP_ENCODER] = "Change Mode,Seq";
        //                               "------------------" <-- Size Guide
    }

private:
    // TODO: reset vs. 2nd clock mode

    // Modes
    int mode[2] = {0, -1}; // 0-3: sequences 0-3 with CV octave transpose (0, 1, or 2 octaves). -1: CV picks sequence, -2 = CV quantize mode, -3 = pick random sequence
    int cv[2] = {0, 0};    // 0 -> HEMISPHERE_MAX_CV. RAND mode writes to this (and over-writes input CV)

    // Sequencer
    MiniSeq miniseq[4];

    // UI
    int cursor = 0;

    void DrawScale()
    {
        gfxPrint(0, 15, OC::Strings::note_names_unpadded[GetRootNote(0)]); // we show the scale but can't change it.
        gfxPrint(8, 15, OC::scale_names_short[GetScale(0)]);
    }

    void DrawMode()
    {
        ForEachChannel(ch)
        {
            int ypos = 15 + (30 * ch);
            if (mode[ch] >= 0)
            {
                gfxPrint(0, ypos, "OCT");
            }
            else if (mode[ch] == PICK_MODE)
            {
                gfxPrint(0, ypos, "PICK");
            }
            else if (mode[ch] == RAND_MODE)
            {
                gfxPrint(0, ypos, "RAND");
            }
            else if (mode[ch] == QUAN_MODE)
            {
                gfxPrint(0, ypos, "QUAN");
            }

            int notenum = MIDIQuantizer::NoteNumber(ValueForChannel(ch));
            //int notenum = GetLatestNoteNumber(ch);
            gfxPrint(0, ypos + 10, midi_note_numbers[notenum]);
            //gfxPrintfn(0, ypos + 10, 4, "%03d", ValueForChannel(ch) / 128);

            // cursor when not picking sequence directly
            if (cursor == ch && mode[ch] < 0)
            {
                gfxCursor(0, ypos + 8, 24);
            }
        }
    }

    void DrawIndicator()
    {
        // step
        for (int i = 0; i < 4; i++)
        {
          gfxPrintfn(43, 25 + (10 * i), 3, "%03d", (miniseq[i].GetNote() + 64));
        }

        // arrow indicators
        ForEachChannel(ch)
        {
            int seq = SequenceForChannel(ch);
            if (seq >= 0)
            {
                int x = 28 + (6 * ch);
                int y = 25 + (10 * seq);
                if (mode[ch] >= 0)
                {
                    gfxBitmap(x, y, 8, RIGHT_BTN_ICON);
                    // cursor when picking sequence in OCT mode.
                    if (cursor == ch)
                    {
                        gfxCursor(x + 3, 63, 5);
                    }
                }
                else
                {
                    gfxBitmap(x, y, 8, RIGHT_BTN_ICON_UNFILLED);
                }
            }
        }
    }

    void Advance() {
        for (int seq = 0; seq < 4; seq++) {
            miniseq[seq].Advance();
        }
    }

    void Reset() {
        for (int seq = 0; seq < 4; seq++) {
            miniseq[seq].Reset();
            miniseq[seq].Advance();
        }
    }

    int StepForSequence(int seq) {
        return miniseq[seq].step;
    }

    // uses cv + encoder. -1 if in QUAN mode.
    int SequenceForChannel(int ch)
    {
        if (mode[ch] == QUAN_MODE)
            return -1;

        int seq = mode[ch];
        if (mode[ch] >= 0)
            seq = mode[ch]; // OCT mode
        else
            seq = Proportion(cv[ch], PP_MAX_INPUT_CV, 4); // +1 to give space for the final seq

        // basic bounds checking
        if (seq < 0)
            seq = 0;
        if (seq > 3)
            seq = 3;
        return seq;
    }

    // get pre-quantize CV
    int ValueForChannel(int ch)
    {
        int seq = SequenceForChannel(ch);
        if (seq >= 0)
        {
            int value = QuantizerLookup(ch, miniseq[seq].GetNote() + 64);
            if (mode[ch] >= 0)
            {
                value = value + (Proportion(cv[ch], PP_MAX_INPUT_CV, OCTAVE_RANGE + 1) * OCTAVE_1);
            }
            return value;
        }
        else // QUAN mode... or we were not able to find a sequence.
        {
            int reduced = Proportion(cv[ch], PP_MAX_INPUT_CV, OCTAVE_RANGE * OCTAVE_1);
            return reduced;
        }
    }
};
