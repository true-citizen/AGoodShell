# libmad (MPEG audio decoder), as used by the MP3 player of AGoodShell++

libmad 0.15.1b, Copyright (C) 2000-2004 Underbit Technologies, Inc., GPL version 2 or later
(see COPYING and COPYRIGHT). Only what is needed to decode MPEG Layer III is here
(Layer I/II is left out, `layer12.c` and the decoder front-end too).

Changes: `mad.h` selects the ARM fixed point multiply (`FPM_ARM`, uses SMULL), and
`timer.c` doesn't have `mad_timer_string()` (it needs sprintf). Build flags used by the
player (see `mp3/`): `-DFPM_ARM -DOPT_SPEED -DOPT_SSO -DASO_IMDCT -DASO_ZEROCHECK`.
