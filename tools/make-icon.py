#!/usr/bin/env python3
"""Original Haiku screen capture and annotation icon. MIT licence."""
from icon_art import Drawing, INK, plane

d = Drawing()
p = plane(5, 8, 0.3)
d.shadow([(13,48),(28,56),(62,56),(47,48)])
# Display stand, with floor-contact edges in the Haiku 2:1 / 1:1 directions.
d.shape([(13,48),(28,55.5),(38,45.5),(23,38)], d.gradient('c5cdd0','7c8a94'), INK)
d.shape([(21,35),(27,38),(27,49),(21,46)], d.gradient('cfdadd','8798a3'), INK, 1.5)
d.shape([(5,8),(11,2),(48,13.1),(42,19.1)], d.gradient('e7eff0','aabfc6'), INK)
d.shape([(42,19.1),(48,13.1),(48,42.1),(42,48.1)], d.gradient('6f8b98','3f5968'), INK)
d.shape(p([(0,0),(37,0),(37,29),(0,29)]), d.gradient('c5d9df','7e9cac'), INK)
d.shape(p([(4,4),(33,4),(33,23),(4,23)]), d.gradient('f7fbef','c7dce0'), '476574', 1)
d.shape(p([(4,4),(33,4),(33,8),(4,8)]), '82adb9')
for v,end in [(12,26),(17,22)]:
    d.line(p([(10,v),(end,v)]), '9bb5bc', 1.5)
# Four flat red capture brackets remain broad at Deskbar size.
for points in [[(6,11),(6,9),(10,9)],[(27,9),(31,9),(31,12)],
               [(6,17),(6,21),(10,21)],[(27,21),(31,21),(31,17)]]:
    d.line(p(points), 'b95643', 2)
# Annotation pencil, set apart from the display's edge.
d.shape([(35,49),(48,25),(54,28),(41,52),(33,57)], d.gradient('ffe2a0','cb913c'), INK, 1.8)
d.shape([(48,25),(50,21),(56,24),(54,28)], d.gradient('edbfb0','ba746b'), INK, 1.5)
d.shape([(35,49),(41,52),(33,57)], 'ead8b3', INK, 1.3)
d.line([(33,57),(35,54)], INK, 1.5)
d.save(__file__, 'resources/branding/airshot-icon.hvif')
