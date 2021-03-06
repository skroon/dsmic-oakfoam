#!/usr/bin/env python
# Read in a list of move prediction results and plot the results.
# The input file format is JSON (see plot_example.json).

import sys
import math
import numpy as np
import json
import csv
import matplotlib
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import axes3d
from matplotlib import cm

jd = json.loads("".join(sys.stdin.readlines()))

plot3d = False
if 'type' in jd.keys():
  plot3d = jd['type']=='3d'

if plot3d:
  fig = plt.figure()

  ax = fig.add_subplot(111, projection='3d')

  fig.suptitle(jd['title'])
  ax.set_ylabel('Move Rank')
  ax.set_zlabel('Cumulative Probability')

  xmax = 200
  if 'xmax' in jd.keys():
    xmax = jd['xmax']

  X = []
  Y = []
  Z = []

  for f in jd['data']:
    with open(f['file'], 'rb') as csvfile:
      x = []
      y = []
      reader = csv.reader(csvfile)
      xi = 1
      s = 0
      for row in reader:
        rx = float(row[0])
        ry = float(row[1])
        if rx>xmax:
          s += ry
        else:
          while xi<rx:
            x.append(xi)
            y.append(0.0)
            xi += 1
          x.append(rx)
          y.append(ry)
          xi += 1

      s += sum(y)
      t = 0.0
      z = []
      zz = []
      for yy in y:
        t += yy
        z.append(t/s)
        zz.append(f['z'])

      Y.append(x)
      Z.append(z)
      X.append(zz)

  # ax.plot_wireframe(X, Y, Z, color='b', cmap='jet')
  surf = ax.plot_surface(X, Y, Z, rstride=1, cstride=1, cmap=cm.jet)
  fig.colorbar(surf, shrink=0.5, aspect=5)

  ax.set_zlim(0,1)
  ax.set_ylim(0,xmax)

else: # 2d plot
  fig = plt.figure()
  fig.canvas.set_window_title(jd['title']) 
  plt.title(jd['title'])
  plt.xlabel('Move Rank')
  plt.ylabel('Cumulative Probability')
  plt.grid(True)
  plt.ylim(0,1)
  xmin = 1
  xmax = 50
  if 'xmax' in jd.keys():
    xmax = int(jd['xmax'])
  plt.xlim(xmin,xmax)
  plt.yticks(np.append(np.arange(0,1,0.05),1))
  plt.xticks(np.append(np.arange(xmin,xmax),xmax))
  errk = 0.0
  if 'errk' in jd.keys():
    errk = float(jd['errk'])

  table = []
  table.append(range(xmin,xmax+1))
  tablelabels = []
  tablelabels.append('Move Rank')

  c = 0
  for f in jd['data']:
    with open(f['file'], 'rb') as csvfile:
      x = []
      y = []
      reader = csv.reader(csvfile)
      for row in reader:
        x.append(float(row[0]))
        y.append(float(row[1]))

      s = sum(y)
      t = 0.0
      z = []
      err = []
      err1 = []
      err2 = []
      for yy in y:
        t += yy
        v = t/s
        z.append(v)
        if errk>0:
          e = errk*math.sqrt(v*(1-v)/s)
        else:
          e = 0
        err.append(e)
        err1.append(v+e)
        err2.append(v-e)

      lbl = f['label'] + ' (%.1f%%)' % (z[0]*100)
      col = cm.spectral(c*0.9/(len(jd['data'])-1),1)
      c+=1
      # p = plt.plot(x, z, label = lbl)
      p = plt.plot(x, z, label = lbl, color = col)
      # col = p[0].get_color()
      if errk>0:
        plt.fill_between(x, err1, err2, alpha = 0.2, color = col)
        # plt.errorbar(x, z, yerr = err, fmt='.', color = col)

      td = []
      xi = 0
      for i in table[0]:
        while i > x[xi]:
          xi += 1
        td.append(z[xi])
      table.append(td)
      tablelabels.append(lbl)

  plt.legend(loc=4)

  sys.stdout.write('# ' + jd['title'] + '\n')

  tt = len(tablelabels)
  for i in range(tt):
    sys.stdout.write('# ' + ('     | ')*i + ' '*5 + '+' + ('-'*7)*(tt-i-1) + '- %d: ' % i + tablelabels[i] +'\n')
  sys.stdout.write('#' + (' |----|')*tt + '\n')

  for i in range(len(table[0])):
    sys.stdout.write(' ')
    for j in range(len(table)):
      if j == 0:
        sys.stdout.write(' %6d' % table[j][i])
      else:
        sys.stdout.write(' %6.3f' % table[j][i])
    sys.stdout.write('\n')

plt.show()

