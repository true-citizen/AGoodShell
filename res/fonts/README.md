
unscii-16.hex and unscii-16-full.hex

Downloaded from http://viznut.fi/unscii/ under the GPL license

hangul-blocks.hex is also part of unifont, manually filtered and
re-numbered to work with hangul.h

The font pack has been generated using:

./generator.py  --font-files unscii-16-full.hex hangul-blocks.hex \
  --font-blocks cjk-sym,latin,latin-a,latin-b,greek,cyrilic,hiragana,katakana,cjk-uni,hangul-part \
  --output ../fonts.pack

A full-hangul version (no char composition) can be generated using:

./generator.py  --font-files unscii-16-full.hex hangul-blocks.hex \
  --font-blocks cjk-sym,latin,latin-a,latin-b,greek,cyrilic,hiragana,katakana,cjk-uni,hangul \
  --output ../fonts-ext.pack

The full font pack (for debugging purposes) can be generated using:

./generator.py --font-files unscii-16-full.hex \
  --font-blocks ascii,check,arrows,arrows2,cjk-sym,latin,latin-a,latin-b,greek,cyrilic,hiragana,katakana,cjk-uni,hangul \
  --output ../fonts-full.pack



Thin UI font (Galmuri9)
-----------------------

The Latin, Greek and Cyrillic blocks (and the ascii/symbol blocks embedded in
src/fonts/font_embed.h) use the Galmuri9 bitmap font (SIL OFL 1.1, see
OFL-Galmuri.txt) from https://github.com/Quiple/Galmuri instead of UNSCII,
for a thinner, DS-like look. Characters that Galmuri9 lacks or that are wider
than 8 pixels fall back to UNSCII. Galmuri9.bdf is converted with:

./galmuri2hex.py Galmuri9.bdf galmuri9-latin.hex

and the galmuri hex file must come after unscii-16-full.hex in --font-files
(later files override earlier ones):

./generator.py --font-files unscii-16-full.hex galmuri9-latin.hex hangul-blocks.hex \
  --font-blocks cjk-sym,latin,latin-a,latin-b,greek,cyrilic,hiragana,katakana,cjk-uni,hangul-part \
  --output ../fonts.pack

./generator.py --font-files unscii-16-full.hex galmuri9-latin.hex \
  --font-blocks ascii,check,arrows,arrows2 --output /tmp/ascii.font
