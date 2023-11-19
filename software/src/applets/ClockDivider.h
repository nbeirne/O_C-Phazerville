// Copyright (c) 2018, Jason Justian
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

// #define HEM_CLOCKDIV_MAX 10

#define HEM_INITIAL_DIVISION1 8
#define HEM_INITIAL_DIVISION2 9
#define HEM_DIVISIONS 16
int divisions[] = {-8, -7, -6, -5, -4, -3, -2, 1, 2, 3, 4, 5, 6, 7, 8, 16, 32};

char buffer[60];

class ClockDivider : public HemisphereApplet
{
public:
    const char *applet_name()
    {
        return "Clock Div";
    }

    void Start()
    {
        div[0] = HEM_INITIAL_DIVISION1;
        div[1] = HEM_INITIAL_DIVISION2;
        ForEachChannel(ch)
        {
            count[ch] = 0;
            next_clock[ch] = 0;
        }
        cycle_time = 0;
        cursor = 0;
    }

    int GetDivisionFor(int ch)
    {
        int dv = div[ch] + cv[ch];
        return divisions[constrain(dv, 0, HEM_DIVISIONS)];
    }

    void Controller()
    {
        int this_tick = OC::CORE::ticks;

        // Set division via CV
        ForEachChannel(ch)
        {
            int input = DetentedIn(ch);
            if (input)
            {
                cv[ch] = Proportion(input, HEMISPHERE_MAX_CV, HEM_DIVISIONS);
                cv[ch] = constrain(cv[ch], -HEM_DIVISIONS, HEM_DIVISIONS); // -16->+16
            }
        }

        if (Clock(1))
        { // Reset
            ForEachChannel(ch) count[ch] = 0;
        }

        // The input was clocked; set timing info
        if (Clock(0))
        {
            cycle_time = ClockCycleTicks(0);
            // At the clock input, handle clock division
            ForEachChannel(ch)
            {
                count[ch]++;
                if (GetDivisionFor(ch) > 0)
                { // Positive value indicates clock division
                    if (count[ch] >= GetDivisionFor(ch))
                    {
                        count[ch] = 0; // Reset
                        ClockOut(ch);
                    }
                }
                else
                {
                    // Calculate next clock for multiplication on each clock
                    int clock_every = (cycle_time / abs(GetDivisionFor(ch)));
                    next_clock[ch] = this_tick + clock_every;
                    ClockOut(ch); // Sync
                }
            }
        }

        // Handle clock multiplication
        ForEachChannel(ch)
        {
            if (GetDivisionFor(ch) < 0)
            { // Negative value indicates clock multiplication
                if (this_tick >= next_clock[ch])
                {
                    int clock_every = (cycle_time / abs(GetDivisionFor(ch)));
                    next_clock[ch] += clock_every;
                    ClockOut(ch);
                }
            }
        }
    }

    void View()
    {
        DrawSelector();
    }

    void OnButtonPress()
    {
        CursorAction(cursor, 1);
    }

    void OnEncoderMove(int direction)
    {
        if (!EditMode())
        {
            MoveCursor(cursor, direction, 1);
            return;
        }

        div[cursor] += direction;
        if (div[cursor] > HEM_DIVISIONS)
        {
            div[cursor] = HEM_DIVISIONS;
        }
        if (div[cursor] < 0)
        {
            div[cursor] = 0;
        }
        count[cursor] = 0; // Start the count over so things aren't missed
    }

    uint64_t OnDataRequest()
    {
        uint64_t data = 0;
        Pack(data, PackLocation{0, 8}, div[0] + 32);
        Pack(data, PackLocation{8, 8}, div[1] + 32);
        return data;
    }

    void OnDataReceive(uint64_t data)
    {
        div[0] = Unpack(data, PackLocation{0, 8}) - 32;
        div[1] = Unpack(data, PackLocation{8, 8}) - 32;
    }

protected:
    void SetHelp()
    {
        help[HEMISPHERE_HELP_DIGITALS] = "1=Clock 2=Reset";
        help[HEMISPHERE_HELP_CVS] = "Div/Mult Ch1,Ch2";
        help[HEMISPHERE_HELP_OUTS] = "Clk A=Ch1 B=Ch2";
        help[HEMISPHERE_HELP_ENCODER] = "Div,Mult";
    }

private:
    int div[2] = {1, 2};        // Division data for outputs. Positive numbers are divisions, negative numbers are multipliers
    int cv[2];                  // cv inputs (range from -16 to +16)
    int count[2] = {0, 0};      // Number of clocks since last output (for clock divide)
    int next_clock[2] = {0, 0}; // Tick number for the next output (for clock multiply)
    int cursor = 0;             // Which output is currently being edited
    int cycle_time = 0;         // Cycle time between the last two clock inputs

    void DrawSelector()
    {
        ForEachChannel(ch)
        {
            int y = 15 + (ch * 25);

            if (GetDivisionFor(ch) > 0)
            {
                gfxPrint(1, y, "/");
                gfxPrint(GetDivisionFor(ch));
                gfxPrint(" Div");
            }
            if (GetDivisionFor(ch) < 0)
            {
                gfxPrint(1, y, "x");
                gfxPrint(abs(GetDivisionFor(ch)));
                gfxPrint(" Mult");
            }
        }
        gfxCursor(0, 23 + (cursor * 25), 63);
    }
};
