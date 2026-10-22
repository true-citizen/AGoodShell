
import sys, PIL.Image

def cc(r, g, b):
  return ( ((r >> 3) << 0) | ((g >> 3) << 5) | ((b >> 3) << 10) )

def ccor(r, g, b):
  # Round to nearest
  r = (r + 4) // 8 * 8
  g = (g + 4) // 8 * 8
  b = (b + 4) // 8 * 8
  return (r, g, b)

im = PIL.Image.open(sys.argv[1])
pixels = im.load()

# Remove color precision
for y in range(im.height):
  for x in range(im.width):
    r, g, b, a = pixels[x, y]
    if a > 128:
      pixels[x, y] = (*ccor(r, g, b), 255)
    else:
      pixels[x, y] = (0,0,0,0)

nicons = im.size[0] // 16

# Icons used by the firmware, in the order of the generated enum. Their image is
# the column of icons.png at the given index. Artwork in icons.png that no menu
# uses anymore (the old tab bar icons and folder/hidden file icons) is not
# converted, to not waste ROM.
iconlst = [
  ("ICON_BINFILE", None, 1),
  ("ICON_UPDFILE", None, 2),
  ("ICON_GBCART", None, 3),
  ("ICON_GBCCART", None, 4),
  ("ICON_GBACART", None, 5),
  ("ICON_SMSCART", None, 6),
  ("ICON_NESCART", None, 7),
  ("ICON_DISK", None, 9),
]

used = [x[2] for x in iconlst]
pal = sorted(set(im.crop((n*16, 0, n*16+16, 16)).getpixel((x, y)) for n in used for x in range(16) for y in range(16)
                 if im.crop((n*16, 0, n*16+16, 16)).getpixel((x, y))[3] > 128))

print("enum {")
for n, c, _ in iconlst:
  if c:
    print("#ifdef %s" % c)
  print(" %s," % n)
  if c:
    print("#endif")
print("};")

print("const uint8_t icons_img[][4][8][8] = {")

for k, (_, cond, n) in enumerate(iconlst):
  if cond:
    print("#ifdef %s" % cond)
  print("  {")
  for subobj in range(4):
    sx, sy = subobj & 1, subobj >> 1
    icon = im.crop((n*16+sx*8, sy*8, n*16+(sx+1)*8, (sy+1)*8))

    print("    {")
    for r in range(8):
      line = []
      for c in range(8):
        col = icon.getpixel((c, r))
        if col[3] > 128:
          line.append(pal.index(col) + 1)
        else:
          line.append(0)
      print("     {" + ",".join("0x%02x" % x for x in line) + "},")
    print("    },")
  print("  },")
  if cond:
    print("#endif")
print("};")

print("const uint16_t icons_pal[%d] = {" % (len(pal)+1))
print("  0x0000," + ",".join("0x%04x" % cc(x[0],x[1],x[2]) for x in pal))
print("};")

