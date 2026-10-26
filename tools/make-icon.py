#!/usr/bin/env python3
"""Original Haiku snapshot camera and annotation pencil. MIT licence."""
from icon_art import Drawing, INK

d = Drawing()
d.shadow([(8,43),(43,55),(62,55),(28,43)])
# Compact camera with rounded shoulders; the dark right edge gives modest depth.
d.shape([(46,20),(53,16),(56,20),(56,43),(50,49),(46,46)],
        d.gradient('829ba5','425c6d'),INK)
d.shape([
    ((6,22),(6,22),(6,19)),
    ((10,18),(7,18),(10,18)),
    (17,19),(21,11),
    ((25,10),(23,9),(25,10)),
    (33,11.5),(38,19),(47,20.5),
    ((51,25),(50,21),(51,25)),
    (51,46),
    ((47,50),(51,50),(47,50)),
    (10,44),
    ((6,40),(6,44),(6,40)),
],d.gradient('f2f5e8','b0c4c9',(10,13),(48,50)),INK)
# Viewfinder and one small shutter control, with no extraneous camera detail.
d.rounded_rect(23,13,9,4,1,'526d7b')
d.rounded_rect(9,15,6,2.5,0.8,d.gradient('e4d0a8','a58c66'))
# Lens: dark barrel, silver rim and blue glass. Gloss is confined to curved glass.
d.ellipse(26,35,14,14.5,d.gradient('637d8c','2e475e'),INK,1.5)
d.ellipse(24.5,33.5,13,14,d.gradient('f1f5e7','94afb9',(14,20),(35,47)),INK,1.7)
d.ellipse(24.5,33.5,9.5,10.5,d.gradient('a0dae2','316b9f',(18,24),(31,42)),'355b78',1.2)
d.line([(21,27),(19,31)],'d9f5ee',1.8,lod=(0.5,4))
# The pencil preserves airShot's annotation function alongside the snapshot allegory.
d.shape([(36,49),(49,25),(55,28),(42,52),(34,58)],d.gradient('ffe3a3','cc963d'),INK,1.8)
d.shape([(49,25),(51,21),(57,24),(55,28)],d.gradient('f0c5b5','be7b6b'),INK,1.5)
d.shape([(36,49),(42,52),(34,58)],'eddbb6',INK,1.3)
d.line([(34,58),(36,55)],INK,1.4)
d.save(__file__, 'resources/branding/airshot-icon.hvif')
