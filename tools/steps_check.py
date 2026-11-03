#!/usr/bin/env python3
"""Host checks of steps( (symce/bin/host/engine_cli, prefix T): the step log of a command is
hand-checked golden transcripts, and for a corpus the answer of steps(E) is the answer of E.
Run: python3 tools/steps_check.py   (make -C symce bin/host/engine_cli first)"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re
import ineqlib
from ineqlib import CLI

FN = {'sin⁻¹(': 0xC3, 'cos⁻¹(': 0xC5, 'tan⁻¹(': 0xC7, 'π': 0xAC, 'sin(': 0xC2, 'cos(': 0xC4, 'tan(': 0xC6, 'ln(': 0xBE, 'e^(': 0xBF, 'log(': 0xC0, 'abs(': 0xB2}
ineqlib.DEC.update({v: k for k, v in FN.items()})
ineqlib.DEC.update({0xEF: '<EF>', 0xDF: "'", 0xAE: "'", 0x06: '[', 0x07: ']'})
ineqlib.ENC.update({'[': 0x06, ']': 0x07})   # stepsgeo: matrices
ineqlib.ENC['ᴇ'] = 0x3B
ineqlib.ENC['π'] = 0xAC
ineqlib.DEC[0xAC] = 'π'
ineqlib.DEC[0xDE] = 'e'
ineqlib.DEC[0x3B] = 'ᴇ'


def enc(s):
    out = b''
    for part in re.split('(' + '|'.join(re.escape(k) for k in FN) + '|e(?!\\^\\())', s):
        out += bytes([FN[part]]) if part in FN else b'\xbb\x31' if part == 'e' else ineqlib.enc(part)
    return out


COS = bytes([0xBB, 0xB2, 0xBB, 0xBF, 0xBB, 0xC3, 0x10])      # the engine spells cos( in letters


def dec(b):
    return ineqlib.dec(bytes(b).replace(COS, b'\xc4').replace(b'\xbb\x31', b'\xde'))


def sdec(b):                                     # a logged step: token 0x2C is i, the comma is 0x2B
    return dec(bytes(b).replace(b'\x2c', b'\xd7'))

RULES = ['Subtract', 'Add', 'Divide both sides by', 'Multiply both sides by', 'Each term', 'Power rule', 'Chain rule',
         'Product rule', 'Add the results', 'Derivative of the top', 'Derivative of the bottom', 'Quotient rule',
         'Move everything', 'Factor the left side', 'Set to 0', 'Discriminant', 'No real roots', 'Common factor', 'Root', 'Divide by prime',
         'First', 'Integrate', 'Top limit', 'Bottom limit', 'Top minus bottom', 'Dominant term', 'Put in', 'Leading terms',
         'Product F', 'Differentiate', 'Combine', 'Quotient F', 'Inside', 'Expand', 'Swap', 'Nth', 'Put the pivot', 'Scale equation', 'Clear that', 'Get the root', 'Square both', 'Root fails', 'Let u', 'In terms of u', 'Put u', 'By parts', 'and dv', 'Split into', 'Integrate each', 'Solve for', 'Homogeneous part', 'Separate and', 'Use the initial', 'Characteristic', 'Root of that', 'Complex roots', 'Homogeneous', 'Add a particular', 'Solve equation', 'Put that in',
         # stepsalg
         'Find two numbers', 'Split the middle', 'Group', 'Common bracket', 'Write the factors', 'Quadratic formula', 'Discriminant value',
         'Put in the formula', 'Simplify the root', 'Use i', 'Simplify roots', 'No neat factors', 'Square root', 'Rational root test',
         'Divide by the factor',
         # stepsalg 2
         'Multiply every term',
         'Write the power',
         'Multiply out',
         'Combine like terms',
         'Common denominator',
         'Rewrite each fraction',
         'Add the tops',
         'Divide the leading terms',
         'Multiply by the divisor',
         'The quotient',
         'The remainder',
         'Divide, remainder',
         'Remainder 0, GCD',
         'Degree',
         'Coefficients',
         'Left side',
         'Right side',
         'Roots by iteration',
         'Round the roots',
         'Sum formulas',
         'Double angle',
         'Triple angle',
         'Power reducing',
         'Product to sum',
         'Log to ln',
         'Change of base',
         'Euler',
         'Pythagorean',
         'Replace',
         'Nothing to change',
         'Factor the bottom',
         'Numerators', 'Absolute value cases', 'Solve each case', 'Not allowed', 'Multiply by the common denominator', 'Flip the sign', 'Boundary values', 'Test a number',
         # stepsgeo
         'Put the points in the distance formula', 'Subtract inside the brackets', 'Square and add',
         'Square and subtract', 'Write the line as ax + by + c = 0', 'Distance from a point to a line',
         'Slope = rise over run', 'Slope of ax + by + c = 0', 'Parallel: same slope, through the point',
         'Perpendicular slope: flip and negate', 'Point-slope form through the point',
         'Vertical line: no slope', 'Perpendicular to a vertical line', 'Perpendicular to a flat line',
         'Same x, so the line is vertical', 'Both lines as ax + by + c = 0', 'Cancel Y between the lines',
         'Solve for the unknown', 'Put X back into a line', 'Put the point into ax + by + c',
         'Size of the normal: a²+b²', 'Go across the line twice as far',
         'Midpoint: average the coordinates', 'Flat segment: the bisector is vertical',
         'Upright segment: the bisector is flat', 'Section formula: how far along',
         'Move t of the way from P to Q', 'Shoelace cross term', 'Add the cross terms',
         'Half the absolute value', 'Length of the next side', 'Add the lengths',
         'Centroid: average the corners', 'Perpendicular bisector of a side', 'Where the two lines cross',
         'Altitude from a corner', 'Side opposite each corner', 'Weighted average of the corners',
         'Pythagoras: c² = a² + b²', 'Pythagoras: a² = c² - b²', 'Circle: (X-h)²+(Y-k)² = r²',
         'Radius squared from a point', 'Divide through by the X² coefficient',
         'Group the X terms and the Y terms', 'Complete the square in X and in Y',
         'Factor each group into a square', 'Subtract the centre', 'Cosine and sine of the angle',
         'New x = x cos - y sin', 'New y = x sin + y cos', 'Add the centre back',
         'Multiply by the scale factor k', 'The answer', 'Write the answer', 'Work it out',
         'Determinant of 2x2: ad - bc', 'Entry times its minor, with sign',
         'Minor: cross out its row and column', 'The determinant', 'Add the terms',
         'Swap a and d, flip the signs of b and c', 'Matrix of cofactors',
         'Swap rows and columns: the adjugate', 'Divide every entry by the determinant', 'Swap rows',
         'Scale a row', 'Clear the column', 'Already in reduced form', 'Rows become columns',
         'Multiply matching entries', 'Work out each product', 'Cross product, one part at a time',
         'Length: root of the sum of squares',
         # stepscalc
         'K sum',
         'K const',
         'K x',
         'K cx',
         'K pow',
         'K root',
         'K sin',
         'K cos',
         'K tan',
         'K exp',
         'K ln',
         'K chain1',
         'K any',
         'K keep',
         'K prod',
         'K quot',
         'K put',
         'K qput',
         'K simp',
         'K add',
         'K ord',
         'K at',
         'K chain',
         'K out',
         'K in',
         'K mul',
         'I sum',
         'I const',
         'I pow',
         'I root',
         'I sin',
         'I cos',
         'I exp',
         'I lin',
         'I keep',
         'I any',
         'I pu',
         'I pv',
         'I ps',
         'I pf',
         'I add',
         'I c',
         'I fb',
         'I fa',
         'I fd',
         'I usub',
         'I uint',
         'I uback',
         'I pfr',
         'I pfi',
         'L dir',
         'L zero',
         'L zero n',
         'L cancel',
         'L cancel2',
         'L series',
         'L top smaller',
         'L same',
         'L bottom smaller',
         'L dominant',
         'L inf',
         'L inf zero',
         'L inf same',
         'L inf big',
         'T deriv',
         'T slope',
         'T height',
         'T nslope',
         'T line',
         'T simp',
         'C plus',
         'C minus',
         'C diff',
         'Y term',
         'Y sum',
         'S series',
         'S top',
         'S bottom',
         'S divide',
         'M side',
         'M dx',
         'M dy',
         'M slope',
         'M next',
         'U list',
         'U values',
         'U sum',
         'P list',
         'P values',
         'P product',
         'U formula',
         'F deriv',
         'F root',
         'F end',
         'F min',
         'F max',
         'A deriv',
         'A formula',
         'A root',
         'A int',
         'D form',
         'D integrate',
         'D homog',
         'D particular',
         'D add',
         'D start',
         'D const',
         'D separate',
         'D solvey',
         'D char',
         'D root',
         'D repeat',
         'D complex',
         'Difference of squares', 'Perfect square', 'Add and subtract', 'Write', 'Same answer, written', "Pascal's triangle", 'Write the terms',
         # stepsfx
         'X let', 'X back', 'X no', 'X ln', 'X ln down', 'X simplify', 'X combine', 'X raise', 'X same', 'X domain', 'X ln both', 'X root',
         'T in sin', 'T in cos', 'T in tan', 'T all sin', 'T all cos', 'T period', 'T solve', 'T range', 'T bounds', 'T let', 'T back', 'T all',
         'K asin',
         'K acos',
         'K atan',
         'K powc',
         'K powb',
         'K powl',
         'I atan',
         'I asin',
         'L conj',
         'L abs',
         'L put',
         'L grow',
         'L zero',
         'L equal',
         'L inf',
         'L squeeze',
         'L osc',
         'L pow',
         'L one',
         'L exp',
         'L answer', 'S geo', 'S gsum', 'S ginf', 'S ps', 'S pf', 'S tel',
         # gaps-de
         'D lp',
         'D factor',
         'D multiply',
         'D int',
         'D exp',
         'D inv',
         # end stepscalc
         ]


SEEN = set()


class Cli:
    def __init__(self):
        self.p = subprocess.Popen([CLI], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def run(self, entry, pre=''):
        self.p.stdin.write(pre + enc(entry).hex() + '\n'); self.p.stdin.flush()
        return self.p.stdout.readline().strip()

    def plain(self, entry, pre=''):
        r = self.run(entry, pre)
        return r if r in ('PASS',) or r.startswith('E') else dec(bytes.fromhex(r))

    def steps(self, entry, pre=''):
        """-> (answer text, [(rule, param, expr)], flags) or None when there is no log"""
        a = self.run(entry, 'T' + pre)
        s = self.p.stdout.readline().strip()
        if a == 'PASS' or (a.startswith('E') and s == 'S-'): return a, None, 0
        if not a.startswith('E'): a = dec(bytes.fromhex(a))               # the answer as the engine wrote it
        if s == 'S-': return a, None, 0
        b = bytes([0]) + bytes.fromhex(s[1:])
        cnt, flags = b[1], b[2]
        i = 70
        out = []
        for _ in range(cnt):
            rule, pl = b[i] | (b[i + 1] >> 7) << 8, b[i + 1] & 127
            par = sdec(b[i + 2:i + 2 + pl]); i += 2 + pl; SEEN.add((rule, par))
            el = b[i]; ex = sdec(b[i + 1:i + 1 + el]); i += 1 + el
            out.append((RULES[rule], par, ex))
        al = b[i]
        assert i + 1 + al == (b[68] | b[69] << 8), 'step walk ends at %d, log says %d' % (i + 1 + al, b[68] | b[69] << 8)   # every plen&127 is the real length
        self.logans = sdec(b[i + 1:i + 1 + al])
        return a, out, flags


GOLDEN = [
    ('SOLVE(~2X+3>7,X)', 'X<-2', [('Move everything', '', '-2X-4>0'), ('Add', '4', '-2X>4'), ('Flip the sign', '', ''), ('Divide both sides by', '-2', 'X<-2')]),
    ('SOLVE(X^2-4>0,X)', 'X<-2 or X>2', [('Boundary values', '', 'X=-2 or X=2'), ('Test a number', '', '')]),
    ('SOLVE(abs(X-1)=3,X)', 'X=-2 or X=4', [('Absolute value cases', '', 'X-1=3 or X-1=-3'), ('Solve each case', '', 'X=-2 or X=4')]),
    ('SOLVE(abs(2X-1)<3,X)', '-1<X<2', [('Absolute value cases', '', '-3<2X-1<3'), ('Solve each case', '', '-1<X<2')]),
    ('SOLVE(1/X=2,X)', 'X=1/2', [('Not allowed', '', 'X=0'), ('Multiply by the common denominator', '', '1-2X=0')]),
    ('SOLVE(1/X>2,X)', '0<X<1/2', [('Not allowed', '', 'X=0'), ('Boundary values', '', 'X=1/2'), ('Test a number', '', '')]),
    ('EXPAND((X+1)(X-2))', 'X²-X-2', [('Multiply every term', '', 'X*X+X*(-2)+1*X+1*(-2)'), ('Combine like terms', '', 'X²-X-2')]),
    ('EXPAND((X+1)^3)', 'X³+3X²+3X+1', [('Write the power', '', '(X+1)(X+1)(X+1)'), ('Multiply every term', '', 'X*X+X*1+1*X+1*1'), ('Combine like terms', '', 'X²+2X+1'), ('Multiply every term', '', 'X²*X+X²*1+2X*X+2X*1+1*X+1*1'), ('Combine like terms', '', 'X³+3X²+3X+1')]),
    ('EXPAND(2(X+3)+X(X-1))', 'X²+X+6', [('Multiply out', '2(X+3)', '2X+6'), ('Multiply out', 'X(X-1)', 'X²-X'), ('Combine like terms', '', 'X²+X+6')]),
    ('COMDENOM(1/X+1/(X+1))', '(2X+1)/(X²+X)', [('Common denominator', '', 'X(X+1)'), ('Rewrite each fraction', '', '(X+1)/(X(X+1))+X/(X(X+1))'), ('Add the tops', '', '((X+1)+X)/(X(X+1))'), ('Combine like terms', '', '(2X+1)/(X²+X)')]),
    ('POLYQUOTIENT(X^3+2X+1,X-1,X)', 'X²+X+3', [('Divide the leading terms', 'X³,X,X²', 'X²'), ('Multiply by the divisor', 'X²', 'X²+2X+1'), ('Divide the leading terms', 'X²,X,X', 'X²+X'), ('Multiply by the divisor', 'X', '3X+1'), ('Divide the leading terms', '3X,X,3', 'X²+X+3'), ('Multiply by the divisor', '3', '4'), ('The quotient', '', 'X²+X+3')]),
    ('POLYGCD(X^2-1,X^2+2X+1,X)', 'X+1', [('Divide, remainder', 'X^2-1,X^2+2X+1', '-2X-2'), ('Divide, remainder', 'X^2+2X+1,-2X-2', '0'), ('Remainder 0, GCD', '', 'X+1')]),
    ('POLYDEGREE((X+1)^3,X)', '3', [('Expand', '', 'X³+3X²+3X+1'), ('Degree', 'X', '3')]),
    ('LEFT(X+1=3)', 'X+1', [('Left side', '', 'X+1')]),
    ('TEXPAND(sin(2X))', '2sin(X)cos(X)', [('Double angle', '', ''), ('Replace', '', '2sin(X)cos(X)')]),
    ('TOLN(log(X))', 'ln(X)/ln(10)', [('Log to ln', '', ''), ('Replace', '', 'ln(X)/ln(10)')]),
    ('PARTFRAC(1/(X^2-1),X)', '1/(2(X-1))-1/(2(X+1))', [('Factor the bottom', '', '(X-1)(X+1)'), ('Numerators', '', '1/(2(X-1))-1/(2(X+1))')]),
    ('SOLVE(2X+3=7,X)', 'X=2', [('Subtract', '3', '2X=4'), ('Divide both sides by', '2', 'X=2')]),
    ('SOLVE(2(X+1)=8,X)', 'X=3', [('Expand', '', '2X+2=8'), ('Subtract', '2', '2X=6'), ('Divide both sides by', '2', 'X=3')]),
    ('SOLVE(5=2X+1,X)', 'X=2', [('Swap', '', '2X+1=5'), ('Subtract', '1', '2X=4'), ('Divide both sides by', '2', 'X=2')]),
    ('SOLVE(X/2+1=3,X)', 'X=4', [('Subtract', '1', 'X/2=2'), ('Multiply both sides by', '2', 'X=4')]),
    ('SOLVE(3X-5=2X+1,X)', 'X=6', [('Subtract', '2X', 'X-5=1'), ('Add', '5', 'X=6')]),
    ('SOLVE(~2X+3=7,X)', 'X=-2', [('Subtract', '3', '-2X=4'), ('Divide both sides by', '-2', 'X=-2')]),
    ('SOLVE(~X=3,X)', 'X=-3', [('Multiply both sides by', '-1', 'X=-3')]),
    ('SOLVE(X+1=1,X)', 'X=0', [('Subtract', '1', 'X=0')]),
    ('SOLVE(X/3-1/2=1,X)', 'X=9/2', [('Add', '1/2', 'X/3=3/2'), ('Multiply both sides by', '3', 'X=9/2')]),
    ('DERIV(X^3+2X,X)', '3X²+2', [('K sum', '', ''), ('K pow', 'X³', '3X²'), ('K cx', '2X', '2'), ('K add', '', '3X²+2')]),
    ('DERIV(X^2+1,X)', '2X', [('K sum', '', ''), ('K pow', 'X²', '2X'), ('K const', '1', '0'), ('K add', '', '2X')]),
    ('DERIV(X^2*sin(X),X)', 'X²cos(X)+2Xsin(X)', [('K prod', 'X²,sin(X)', ''), ('K pow', 'X²', '2X'), ('K sin', 'sin(X)', 'cos(X)'), ('K put', '', '2Xsin(X)+X²cos(X)'), ('K simp', '', 'X²cos(X)+2Xsin(X)')]),
    ('DERIV(X^3,X,2)', '6X', [('K ord', '1,2', ''), ('K pow', 'X³', '3X²'), ('K ord', '2,2', ''), ('K pow', 'X²', '2X'), ('K keep', '3,2X', '6X')]),
    ('DERIV(sin(2X),X)', '2cos(2X)', [('K chain', 'sin(2X),2X', ''), ('K out', '', 'cos(2X)'), ('K in', '2X', '2'), ('K mul', 'cos(2X),2', '2cos(2X)')]),
    ('DERIV(sin(X)+X,X)', 'cos(X)+1', [('K sum', '', ''), ('K sin', 'sin(X)', 'cos(X)'), ('K x', 'X', '1'), ('K add', '', 'cos(X)+1')]),
    ('DERIV((X+1)/(X-1),X)', '-2/(X²-2X+1)', [('K quot', 'X+1,X-1', ''), ('K any', 'X+1', '1'), ('K any', 'X-1', '1'), ('K qput', '', '((X-1)-(X+1))/(X-1)²'), ('K simp', '', '-2/(X²-2X+1)')]),
    ('SOLVE(X^2-5X+6=0,X)', 'X=2 or X=3', [('Find two numbers', '6,-5,-3,-2', ''), ('Factor the left side', '', '(X-3)(X-2)=0'), ('Set to 0', 'X-2', 'X=2'), ('Set to 0', 'X-3', 'X=3')]),
    ('SOLVE(2X^2=8X,X)', 'X=0 or X=4', [('Move everything', '', '2X²-8X=0'), ('Divide both sides by', '2', 'X²-4X=0'), ('Factor the left side', '', 'X(X-4)=0'), ('Set to 0', 'X', 'X=0'), ('Set to 0', 'X-4', 'X=4')]),
    ('SOLVE(X^2-2X-1=0,X)', 'X=1-√(2) or X=1+√(2)', [('Quadratic formula', '1,-2,-1', ''), ('Discriminant value', '', '(-2)^2-4*1*(-1)=8'), ('Put in the formula', '', 'X=(2-√(8))/2 or X=(2+√(8))/2'), ('Simplify the root', '', '√(8)=2√(2)'), ('Simplify roots', '', 'X=1-√(2) or X=1+√(2)')]),
    ('SOLVE(X^3=X,X)', 'X=-1 or X=0 or X=1', [('Move everything', '', 'X³-X=0'), ('Common factor', '', 'X(X²-1)'), ('Find two numbers', '-1,0,-1,1', ''), ('Factor the left side', '', 'X(X-1)(X+1)=0'), ('Set to 0', 'X+1', 'X=-1'), ('Set to 0', 'X', 'X=0'), ('Set to 0', 'X-1', 'X=1')]),
    ('FACTOR((X^2+X)/2)', 'X(X+1)/2', [('Common factor', '', 'X(X+1)/2')]),   # not X/2(X+1)
    ('FACTOR(2X^3-8X)', '2X(X-2)(X+2)', [('Common factor', '', '2X(X²-4)'), ('Find two numbers', '-4,0,-2,2', ''), ('Write the factors', '', '(X-2)(X+2)')]),
    ('FACTOR(X^3-6X^2+11X-6)', '(X-3)(X-2)(X-1)', [('Rational root test', '6,1', ''), ('Root', 'X=1', 'X-1'), ('Divide by the factor', 'X-1', 'X²-5X+6'), ('Find two numbers', '6,-5,-3,-2', ''), ('Write the factors', '', '(X-3)(X-2)')]),
    ('FACTOR(60)', '2²*3*5', [('Divide by prime', '2', '30'), ('Divide by prime', '2', '15'), ('Divide by prime', '3', '5'), ('Divide by prime', '5', '1')]),
    ('INTEGRAL(3X^2+2X,X)', 'X³+X²', [('I sum', '', ''), ('I pow', 'X²,3', 'X³/3'), ('I keep', '3,X³/3', 'X³'), ('I pow', 'X,2', 'X²/2'), ('I keep', '2,X²/2', 'X²'), ('I add', '', 'X³+X²'), ('I c', '', 'X³+X²+C')]),
    ('INTEGRAL(X^2,X,0,2)', '8/3', [('I pow', 'X²,3', 'X³/3'), ('I fb', '2', '2³/3=8/3'), ('I fa', '0', '0³/3=0'), ('I fd', '0,2', '8/3-0=8/3')]),
    ('INTEGRAL(sin(X)+X,X,0,2)', '3-cos(2)', [('I sum', '', ''), ('I sin', 'sin(X)', '-cos(X)'), ('I pow', 'X,2', 'X²/2'), ('I add', '', 'X²/2-cos(X)'), ('I fb', '2', '2²/2-cos(2)=2-cos(2)'), ('I fa', '0', '0²/2-cos(0)=-1'), ('I fd', '0,2', '2-cos(2)-(-1)=3-cos(2)')]),
    ('SOLVE({X+Y=3,X-Y=1},{X,Y})', 'X=2<40>Y=1', [('Clear that', '2', '-2Y=-2'), ('Scale equation', '2', 'Y=1'), ('Clear that', '1', 'X=2')]),
    ('SOLVE({Y=2,X+Y=3},{X,Y})', 'X=1<40>Y=2', [('Put the pivot', '1', 'X+Y=3'), ('Clear that', '1', 'X=1')]),
    ('SOLVE(√X+1)=3,X)', 'X=8', [('Get the root', '', '√(X+1)=3'), ('Square both', '', '9=X+1'), ('Swap', '', 'X+1=9'), ('Subtract', '1', 'X=8')]),
    ('SOLVE(√X)=X-2,X)', 'X=4', [('Get the root', '', '√(X)=X-2'), ('Square both', '', 'X²-4X+4=X'), ('Move everything', '', 'X²-5X+4=0'), ('Find two numbers', '4,-5,-4,-1', ''), ('Factor the left side', '', '(X-4)(X-1)=0'), ('Set to 0', 'X-1', 'X=1'), ('Set to 0', 'X-4', 'X=4'), ('Root fails', '1', '')]),
    ('INTEGRAL(X*sin(X),X)', 'sin(X)-Xcos(X)', [('I pu', 'X,sin(X)', ''), ('I pv', '1,-cos(X)', ''), ('I ps', '-cos(X)', '-sin(X)'), ('I pf', '', 'sin(X)-Xcos(X)'), ('I c', '', 'sin(X)-Xcos(X)+C')]),
    ('INTEGRAL(ln(X),X)', 'Xln(X)-X', [('I pu', 'ln(X),1', ''), ('I pv', '1/X,X', ''), ('I ps', '1', 'X'), ('I pf', '', 'Xln(X)-X'), ('I c', '', 'Xln(X)-X+C')]),
    ('INTEGRAL(√2X+1),X,0,4)', '26/3', [('I usub', '2X+1,2', ''), ('I uint', '', '√(U)/2'), ('I root', '√(U)', '2U√(U)/3'), ('I keep', '1/2,2U√(U)/3', 'U√(U)/3'), ('I uback', '2X+1', '2X√(2X+1)/3+√(2X+1)/3'), ('I fb', '4', '2(4)√(2(4)+1)/3+√(2(4)+1)/3=9'), ('I fa', '0', '2(0)√(2(0)+1)/3+√(2(0)+1)/3=1/3'), ('I fd', '0,4', '9-1/3=26/3')]),
    ('INTEGRAL(X/(X+1),X)', 'X-ln(abs(X+1))', [('I pfr', '', '1-1/(X+1)'), ('I pfi', '', 'X-ln(abs(X+1))'), ('I c', '', 'X-ln(abs(X+1))+C')]),
    ('INTEGRAL(1/(X^2-1),X,2,3)', 'ln(3)/2-ln(4)/2+ln(2)/2', [('I pfr', '', '1/(2(X-1))-1/(2(X+1))'), ('I pfi', '', 'ln(abs(X-1))/2-ln(abs(X+1))/2'), ('I fb', '3', 'ln(abs(3-1))/2-ln(abs(3+1))/2=ln(2)/2-ln(4)/2'), ('I fa', '2', 'ln(abs(2-1))/2-ln(abs(2+1))/2=-ln(3)/2'), ('I fd', '2,3', 'ln(2)/2-ln(4)/2-(-ln(3)/2)=ln(3)/2+ln(2)/2-ln(4)/2')]),
    ('SOLVE({Y=X²,X+Y=2},{X,Y})', '{X=-2,Y=4},{X=1,Y=1}', [('Solve equation', '1', 'Y=X²'), ('Put that in', '', 'X²+X-2=0'), ('Find two numbers', '-2,1,-1,2', ''), ('Factor the left side', '', '(X-1)(X+2)=0'), ('Set to 0', 'X+2', 'X=-2'), ('Set to 0', 'X-1', 'X=1')]),
    ('FACTOR(X^2+X+1)', 'X²+X+1', [('Discriminant value', '', '1^2-4*1*1=-3'), ('No real roots', '', '')]),
    ('DISTANCE((1,2),(4,6))', '5', [('Put the points in the distance formula', '', '√((4-1)²+(6-2)²)'), ('Subtract inside the brackets', '', '√(3²+4²)'), ('Square and add', '', '√(25)'), ('The answer', '', '5')]),
    ('DISTANCE((~1,2),(2,~2))', '5', [('Put the points in the distance formula', '', '√((2-(-1))²+((-2)-2)²)'), ('Subtract inside the brackets', '', '√(3²+(-4)²)'), ('Square and add', '', '√(25)'), ('The answer', '', '5')]),
    ('DISTANCE((1,1),Y=X+3)', '3√(2)/2', [('Write the line as ax + by + c = 0', '', '-X+Y-3=0'), ('Distance from a point to a line', '', 'abs((-1)*1+1*1+(-3))/√((-1)²+1²)'), ('Work it out', '', 'abs(-3)/√(2)'), ('The answer', '', '3√(2)/2')]),
    ('DISTANCE((0,0),3X+4Y=10)', '2', [('Write the line as ax + by + c = 0', '', '3X+4Y-10=0'), ('Distance from a point to a line', '', 'abs(3*0+4*0+(-10))/√(3²+4²)'), ('Work it out', '', 'abs(-10)/√(25)'), ('The answer', '', '2')]),
    ('SLOPE((1,2),(4,8))', '2', [('Slope = rise over run', '', '(8-2)/(4-1)=2')]),
    ('SLOPE(2X+3Y=6)', '-2/3', [('Write the line as ax + by + c = 0', '', '2X+3Y-6=0'), ('Slope of ax + by + c = 0', '', '-2/3')]),
    ('PARALLEL((1,3),Y=2X+1)', 'Y=2X+1', [('Write the line as ax + by + c = 0', '', '-2X+Y-1=0'), ('Slope of ax + by + c = 0', '', '-(-2)/1=2'), ('Parallel: same slope, through the point', '', 'Y-3=2*(X-1)'), ('Write the answer', '', 'Y=2X+1')]),
    ('PARALLEL((1,3),X=5)', 'X=1', [('Write the line as ax + by + c = 0', '', 'X-5=0'), ('Vertical line: no slope', '', 'X=1')]),
    ('PERPENDICULAR((1,3),Y=2X+1)', 'Y=7/2-X/2', [('Write the line as ax + by + c = 0', '', '-2X+Y-1=0'), ('Slope of ax + by + c = 0', '', '-(-2)/1=2'), ('Perpendicular slope: flip and negate', '', '-1/2'), ('Point-slope form through the point', '', 'Y-3=(-1/2)*(X-1)'), ('Write the answer', '', 'Y=7/2-X/2')]),
    ('PERPENDICULAR((1,3),X=5)', 'Y=3', [('Write the line as ax + by + c = 0', '', 'X-5=0'), ('Perpendicular to a vertical line', '', 'Y=3')]),
    ('PERPENDICULAR((1,3),Y=4)', 'X=1', [('Write the line as ax + by + c = 0', '', 'Y-4=0'), ('Perpendicular to a flat line', '', 'X=1')]),
    ('INTERSECT(Y=2X+1,Y=~X+7)', '(2,5)', [('Both lines as ax + by + c = 0', '', '-2X+Y-1=0,X+Y-7=0'), ('Cancel Y between the lines', '', '-3X+6=0'), ('Solve for the unknown', '', 'X=2'), ('Put X back into a line', '', 'Y-5=0'), ('Solve for the unknown', '', 'Y=5'), ('The answer', '', '(2,5)')]),
    ('INTERSECT(2X+3Y=6,X-Y=2)', '(12/5,2/5)', [('Both lines as ax + by + c = 0', '', '2X+3Y-6=0,X-Y-2=0'), ('Cancel Y between the lines', '', '-5X+12=0'), ('Solve for the unknown', '', 'X=12/5'), ('Put X back into a line', '', '3Y-6/5=0'), ('Solve for the unknown', '', 'Y=2/5'), ('The answer', '', '(12/5,2/5)')]),
    ('REFLECT((1,2),Y=X)', '(2,1)', [('Write the line as ax + by + c = 0', '', '-X+Y=0'), ('Put the point into ax + by + c', '', '(-1)*1+1*2+0=1'), ('Size of the normal: a²+b²', '', '(-1)²+1²=2'), ('Go across the line twice as far', '', '(1,2)-1*(-1,1)'), ('The answer', '', '(2,1)')]),
    ('REFLECT((3,1),2X+Y=4)', '(3/5,-1/5)', [('Write the line as ax + by + c = 0', '', '2X+Y-4=0'), ('Put the point into ax + by + c', '', '2*3+1*1+(-4)=3'), ('Size of the normal: a²+b²', '', '2²+1²=5'), ('Go across the line twice as far', '', '(3,1)-(6/5)*(2,1)'), ('The answer', '', '(3/5,-1/5)')]),
    ('MIDPOINT((1,2),(5,~8))', '(3,-3)', [('Midpoint: average the coordinates', '', '((1+5)/2,(2-8)/2)'), ('The answer', '', '(3,-3)')]),
    ('PARTITION((1,2),(6,12),2,3)', '(3,6)', [('Section formula: how far along', '', '2/(2+3)=2/5'), ('Move t of the way from P to Q', '', '(1+(2/5)*(6-1),2+(2/5)*(12-2))'), ('The answer', '', '(3,6)')]),
    ('LINE((1,2),(3,8))', 'Y=3X-1', [('Slope = rise over run', '', '(8-2)/(3-1)=3'), ('Point-slope form through the point', '', 'Y-2=3*(X-1)'), ('Write the answer', '', 'Y=3X-1')]),
    ('LINE((1,2),(1,8))', 'X=1', [('Same x, so the line is vertical', '', 'X=1')]),
    ('LINE((1,2),3)', 'Y=3X-1', [('Point-slope form through the point', '', 'Y-2=3*(X-1)'), ('Write the answer', '', 'Y=3X-1')]),
    ('PERPBISECTOR((1,2),(5,6))', 'Y=7-X', [('Midpoint: average the coordinates', '', '((1+5)/2,(2+6)/2)'), ('Work it out', '', '(3,4)'), ('Slope = rise over run', '', '(6-2)/(5-1)=1'), ('Perpendicular slope: flip and negate', '', '-1/1=-1'), ('Point-slope form through the point', '', 'Y-4=(-1)*(X-3)'), ('Write the answer', '', 'Y=7-X')]),
    ('PERPBISECTOR((1,2),(5,2))', 'X=3', [('Midpoint: average the coordinates', '', '((1+5)/2,(2+2)/2)'), ('Work it out', '', '(3,2)'), ('Flat segment: the bisector is vertical', '', 'X=3')]),
    ('PERPBISECTOR((1,2),(1,8))', 'Y=5', [('Midpoint: average the coordinates', '', '((1+1)/2,(2+8)/2)'), ('Work it out', '', '(1,5)'), ('Upright segment: the bisector is flat', '', 'Y=5')]),
    ('AREA((0,0),(4,0),(4,3))', '6', [('Shoelace cross term', '', '0*0-4*0=0'), ('Shoelace cross term', '', '4*3-4*0=12'), ('Shoelace cross term', '', '4*0-0*3=0'), ('Add the cross terms', '', '0+12+0=12'), ('Half the absolute value', '', 'abs(12)/2=6')]),
    ('AREA((0,0),(4,0),(4,3),(0,3))', '12', [('Shoelace cross term', '', '0*0-4*0=0'), ('Shoelace cross term', '', '4*3-4*0=12'), ('Shoelace cross term', '', '4*3-0*3=12'), ('Shoelace cross term', '', '0*0-0*3=0'), ('Add the cross terms', '', '0+12+12+0=24'), ('Half the absolute value', '', 'abs(24)/2=12')]),
    ('PERIMETER((0,0),(4,0),(4,3))', '12', [('Length of the next side', '', '√((4-0)²+(0-0)²)=4'), ('Length of the next side', '', '√((4-4)²+(3-0)²)=3'), ('Length of the next side', '', '√((0-4)²+(0-3)²)=5'), ('Add the lengths', '', '4+3+5=12')]),
    ('CENTROID((0,0),(6,0),(0,9))', '(2,3)', [('Centroid: average the corners', '', '((0+6+0)/3,(0+0+9)/3)'), ('The answer', '', '(2,3)')]),
    ('CIRCUMCENTER((0,0),(4,0),(0,3))', '(2,3/2)', [('Perpendicular bisector of a side', '', '8X-16=0'), ('Perpendicular bisector of a side', '', '-8X+6Y+7=0'), ('Where the two lines cross', '', '(2,3/2)')]),
    ('ORTHOCENTER((0,0),(4,0),(0,3))', '(0,0)', [('Altitude from a corner', '', '-4X+3Y=0'), ('Altitude from a corner', '', '-3Y=0'), ('Where the two lines cross', '', '(0,0)')]),
    ('INCENTER((0,0),(4,0),(0,3))', '(1,1)', [('Side opposite each corner', '', '5,3,4'), ('Weighted average of the corners', '', '(5*0+3*4+4*0)/(5+3+4)=1'), ('Weighted average of the corners', '', '(5*0+3*0+4*3)/(5+3+4)=1'), ('The answer', '', '(1,1)')]),
    ('HYPOT(3,4)', '5', [('Pythagoras: c² = a² + b²', '', '√(3²+4²)'), ('Square and add', '', '√(25)'), ('The answer', '', '5')]),
    ('LEG(5,3)', '4', [('Pythagoras: a² = c² - b²', '', '√(5²-3²)'), ('Square and subtract', '', '√(16)'), ('The answer', '', '4')]),
    ('CIRCLE((1,2),3)', '(X-1)²+(Y-2)²=9', [('Circle: (X-h)²+(Y-k)² = r²', '', '(X-1)²+(Y-2)²=3²'), ('Write the answer', '', '(X-1)²+(Y-2)²=9')]),
    ('CIRCLE((1,2),(4,6))', '(X-1)²+(Y-2)²=25', [('Radius squared from a point', '', '(4-1)²+(6-2)²=25'), ('Circle: (X-h)²+(Y-k)² = r²', '', '(X-1)²+(Y-2)²=25')]),
    ('CIRCLE((0,0),(4,0),(0,3))', '(X-2)²+(Y-3/2)²=25/4', [('Perpendicular bisector of a side', '', '8X-16=0'), ('Perpendicular bisector of a side', '', '-8X+6Y+7=0'), ('Where the two lines cross', '', '(2,3/2)'), ('Radius squared from a point', '', '(0-2)²+(0-(3/2))²=25/4'), ('Circle: (X-h)²+(Y-k)² = r²', '', '(X-2)²+(Y-3/2)²=25/4')]),
    ('CIRCLE(X^2+Y^2-2X+4Y-4=0)', 'C=(1,-2),R=3', [('Group the X terms and the Y terms', '', 'X²+Y²-2X+4Y=4'), ('Complete the square in X and in Y', '', '4+(-1)²+2²=9'), ('Factor each group into a square', '', '(X-1)²+(Y+2)²=9'), ('Write the answer', '', 'C=(1,-2),R=3')]),
    ('CIRCLE(2X^2+2Y^2-4X-8Y-2=0)', 'C=(1,2),R=√(6)', [('Divide through by the X² coefficient', '', 'X²+Y²-2X-4Y-1=0'), ('Group the X terms and the Y terms', '', 'X²+Y²-2X-4Y=1'), ('Complete the square in X and in Y', '', '1+(-1)²+(-2)²=6'), ('Factor each group into a square', '', '(X-1)²+(Y-2)²=6'), ('Write the answer', '', 'C=(1,2),R=√(6)')]),
    ('ROTATE((1,0),90)', '(0,1)', [('Cosine and sine of the angle', '', 'cos(90)=0,sin(90)=1'), ('New x = x cos - y sin', '', '1*0-0*1=0'), ('New y = x sin + y cos', '', '1*1+0*0=1'), ('The answer', '', '(0,1)')]),
    ('ROTATE((2,1),60,(1,1))', '(3/2,1+√(3)/2)', [('Subtract the centre', '', '(2-1,1-1)=(1,0)'), ('Cosine and sine of the angle', '', 'cos(60)=1/2,sin(60)=√(3)/2'), ('New x = x cos - y sin', '', '1*(1/2)-0*(√(3)/2)=1/2'), ('New y = x sin + y cos', '', '1*(√(3)/2)+0*(1/2)=√(3)/2'), ('Add the centre back', '', '((1/2)+1,(√(3)/2)+1)=(3/2,1+√(3)/2)')]),
    ('DILATE((1,2),3)', '(3,6)', [('Multiply by the scale factor k', '', '(3*1,3*2)=(3,6)')]),
    ('DILATE((3,4),2,(1,1))', '(5,7)', [('Subtract the centre', '', '(3-1,4-1)=(2,3)'), ('Multiply by the scale factor k', '', '(2*2,2*3)=(4,6)'), ('Add the centre back', '', '(4+1,6+1)=(5,7)')]),
    ('MDET([[1,2][3,4]])', '-2', [('Determinant of 2x2: ad - bc', '', '1*4-2*3=-2')]),
    ('MDET([[2,0,1][1,3,2][1,1,1]])', '0', [('Minor: cross out its row and column', '', '[[3,2][1,1]]'), ('Determinant of 2x2: ad - bc', '', '3*1-2*1=1'), ('Entry times its minor, with sign', '', '2*1=2'), ('Minor: cross out its row and column', '', '[[1,3][1,1]]'), ('Determinant of 2x2: ad - bc', '', '1*1-3*1=-2'), ('Entry times its minor, with sign', '', '1*(-2)=-2'), ('Add the terms', '', '2-2=0')]),
    ('MINV([[1,2][3,4]])', '[[-2,1][3/2,-1/2]]', [('Determinant of 2x2: ad - bc', '', '1*4-2*3=-2'), ('Swap a and d, flip the signs of b and c', '', '[[4,-2][-3,1]]'), ('Divide every entry by the determinant', '-2', '[[-2,1][3/2,-1/2]]')]),
    ('MTRANS([[1,2,3][4,5,6]])', '[[1,4][2,5][3,6]]', [('Rows become columns', '', '[[1,4][2,5][3,6]]')]),
    ('MRREF([[1,2,3][4,5,6][7,8,10]])', '[[1,0,0][0,1,0][0,0,1]]', [('Clear the column', 'R2=R2-4*R1', '[[1,2,3][0,-3,-6][7,8,10]]'), ('Clear the column', 'R3=R3-7*R1', '[[1,2,3][0,-3,-6][0,-6,-11]]'), ('Scale a row', 'R2=R2/(-3)', '[[1,2,3][0,1,2][0,-6,-11]]'), ('Clear the column', 'R1=R1-2*R2', '[[1,0,-1][0,1,2][0,-6,-11]]'), ('Clear the column', 'R3=R3+6*R2', '[[1,0,-1][0,1,2][0,0,1]]'), ('Clear the column', 'R1=R1+1*R3', '[[1,0,0][0,1,2][0,0,1]]'), ('Clear the column', 'R2=R2-2*R3', '[[1,0,0][0,1,0][0,0,1]]')]),
    ('DOTP([1,2,3],[4,5,6])', '32', [('Multiply matching entries', '', '1*4+2*5+3*6'), ('Work out each product', '', '4+10+18=32')]),
    ('CROSSP([1,2,3],[4,5,6])', '[-3,6,-3]', [('Cross product, one part at a time', '', '2*6-3*5=-3'), ('Cross product, one part at a time', '', '3*4-1*6=6'), ('Cross product, one part at a time', '', '1*5-2*4=-3'), ('The answer', '', '[-3,6,-3]')]),
    ('NORM([3,4])', '5', [('Length: root of the sum of squares', '', '√(3²+4²)'), ('Square and add', '', '√(25)'), ('The answer', '', '5')]),
    ('MINV([[2,1,1][1,3,2][1,0,0]])', '[[0,0,1][-2,1,3][3,-1,-5]]', [('Minor: cross out its row and column', '', '[[3,2][0,0]]'), ('Determinant of 2x2: ad - bc', '', '3*0-2*0=0'), ('Entry times its minor, with sign', '', '2*0=0'), ('Minor: cross out its row and column', '', '[[1,2][1,0]]'), ('Determinant of 2x2: ad - bc', '', '1*0-2*1=-2'), ('Entry times its minor, with sign', '', '-1*(-2)=2'), ('Minor: cross out its row and column', '', '[[1,3][1,0]]'), ('Determinant of 2x2: ad - bc', '', '1*0-3*1=-3'), ('Entry times its minor, with sign', '', '1*(-3)=-3'), ('Add the terms', '', '0+2-3=-1'), ('Matrix of cofactors', '', '[[0,2,-3][0,-1,1][-1,-3,5]]'), ('Swap rows and columns: the adjugate', '', '[[0,0,-1][2,-1,-3][-3,1,5]]'), ('Divide every entry by the determinant', '-1', '[[0,0,1][-2,1,3][3,-1,-5]]')]),
    ('MRREF([[1,0][0,1]])', '[[1,0][0,1]]', [('Already in reduced form', '', '[[1,0][0,1]]')]),
    ('MRREF([[0,1][1,0]])', '[[1,0][0,1]]', [('Swap rows', 'R1,R2', '[[1,0][0,1]]')]),
    ('MDET([[A,B][C,D]])', 'AD-BC', [('Determinant of 2x2: ad - bc', '', 'A*D-B*C=AD-BC')]),
    ('MDET([[1,2,3,4][5,6,7,8][2,6,4,8][3,1,1,2]])', '72', [('Minor: cross out its row and column', '', '[[6,7,8][6,4,8][1,1,2]]'), ('The determinant', '', '-12'), ('Entry times its minor, with sign', '', '1*(-12)=-12'), ('Minor: cross out its row and column', '', '[[5,7,8][2,4,8][3,1,2]]'), ('The determinant', '', '60'), ('Entry times its minor, with sign', '', '-2*60=-120'), ('Minor: cross out its row and column', '', '[[5,6,8][2,6,8][3,1,2]]'), ('The determinant', '', '12'), ('Entry times its minor, with sign', '', '3*12=36'), ('Minor: cross out its row and column', '', '[[5,6,7][2,6,4][3,1,1]]'), ('The determinant', '', '-42'), ('Entry times its minor, with sign', '', '-4*(-42)=168'), ('Add the terms', '', '-12-120+36+168=72')]),
    ('NORM([1,2,3,4,5,6,7,8])', '2√(51)', [('Length: root of the sum of squares', '', '√(1²+2²+3²+4²+5²+6²+7²+8²)'), ('Square and add', '', '√(204)'), ('The answer', '', '2√(51)')]),
    ('MDET([[1,0,0,0][0,1,0,0][0,0,1,0][0,0,0,2]])', '2', [('Minor: cross out its row and column', '', '[[1,0,0][0,1,0][0,0,2]]'), ('The determinant', '', '2'), ('Entry times its minor, with sign', '', '1*2=2')]),
    ('MRREF([[0,1,2][1,0,3]])', '[[1,0,3][0,1,2]]', [('Swap rows', 'R1,R2', '[[1,0,3][0,1,2]]')]),
    ('MRREF([[2,4][1,2]])', '[[1,2][0,0]]', [('Scale a row', 'R1=R1/2', '[[1,2][1,2]]'), ('Clear the column', 'R2=R2-1*R1', '[[1,2][0,0]]')]),
    ('MRREF([[A,1][2,3]])', '[[1,0][0,1]]', [('Scale a row', 'R1=R1/A', '[[1,1/A][2,3]]'), ('Clear the column', 'R2=R2-2*R1', '[[1,1/A][0,(3A-2)/A]]'), ('Scale a row', 'R2=R2/((3A-2)/A)', '[[1,1/A][0,1]]'), ('Clear the column', 'R1=R1-1/A*R2', '[[1,0][0,1]]')]),
    ('MRREF([[3,1,2][1,4,1][2,1,5]])', '[[1,0,0][0,1,0][0,0,1]]', [('Scale a row', 'R1=R1/3', '[[1,1/3,2/3][1,4,1][2,1,5]]'), ('Clear the column', 'R2=R2-1*R1', '[[1,1/3,2/3][0,11/3,1/3][2,1,5]]'), ('Clear the column', 'R3=R3-2*R1', '[[1,1/3,2/3][0,11/3,1/3][0,1/3,11/3]]'), ('Scale a row', 'R2=R2/(11/3)', '[[1,1/3,2/3][0,1,1/11][0,1/3,11/3]]'), ('Clear the column', 'R1=R1-1/3*R2', '[[1,0,7/11][0,1,1/11][0,1/3,11/3]]'), ('Clear the column', 'R3=R3-1/3*R2', '[[1,0,7/11][0,1,1/11][0,0,40/11]]'), ('Scale a row', 'R3=R3/(40/11)', '[[1,0,7/11][0,1,1/11][0,0,1]]'), ('Clear the column', 'R1=R1-7/11*R3', '[[1,0,0][0,1,1/11][0,0,1]]'), ('Clear the column', 'R2=R2-1/11*R3', '[[1,0,0][0,1,0][0,0,1]]')]),
    ('MRREF([[7,3,2][5,11,4][9,2,13]])', '[[1,0,0][0,1,0][0,0,1]]', [('Scale a row', 'R1=R1/7', '[[1,3/7,2/7][5,11,4][9,2,13]]'), ('Clear the column', 'R2=R2-5*R1', '[[1,3/7,2/7][0,62/7,18/7][9,2,13]]'), ('Clear the column', 'R3=R3-9*R1', '[[1,3/7,2/7][0,62/7,18/7][0,-13/7,73/7]]'), ('Scale a row', 'R2=R2/(62/7)', '[[1,3/7,2/7][0,1,9/31][0,-13/7,73/7]]'), ('Clear the column', 'R1=R1-3/7*R2', '[[1,0,5/31][0,1,9/31][0,-13/7,73/7]]'), ('Clear the column', 'R3=R3+13/7*R2', '[[1,0,5/31][0,1,9/31][0,0,340/31]]'), ('Scale a row', 'R3=R3/(340/31)', '[[1,0,5/31][0,1,9/31][0,0,1]]'), ('Clear the column', 'R1=R1-5/31*R3', '[[1,0,0][0,1,9/31][0,0,1]]'), ('Clear the column', 'R2=R2-9/31*R3', '[[1,0,0][0,1,0][0,0,1]]')]),
    ('MRREF([[1,2][3,4][5,6][7,9]])', '[[1,0][0,1][0,0][0,0]]', [('Clear the column', 'R2=R2-3*R1', '[[1,2][0,-2][5,6][7,9]]'), ('Clear the column', 'R3=R3-5*R1', '[[1,2][0,-2][0,-4][7,9]]'), ('Clear the column', 'R4=R4-7*R1', '[[1,2][0,-2][0,-4][0,-5]]'), ('Scale a row', 'R2=R2/(-2)', '[[1,2][0,1][0,-4][0,-5]]'), ('Clear the column', 'R1=R1-2*R2', '[[1,0][0,1][0,-4][0,-5]]'), ('Clear the column', 'R3=R3+4*R2', '[[1,0][0,1][0,0][0,-5]]'), ('Clear the column', 'R4=R4+5*R2', '[[1,0][0,1][0,0][0,0]]')]),
    ('DERIV(3X^2+2X+5,X)', '6X+2', [('K sum', '', ''), ('K pow', 'X²', '2X'), ('K keep', '3,2X', '6X'), ('K cx', '2X', '2'), ('K const', '5', '0'), ('K add', '', '6X+2')]),
    ('DERIV(sin(X^2),X)', '2Xcos(X²)', [('K chain', 'sin(X²),X²', ''), ('K out', '', 'cos(X²)'), ('K in', 'X²', '2X'), ('K mul', 'cos(X²),2X', '2Xcos(X²)')]),
    ('DERIV(3sin(2X),X)', '6cos(2X)', [('K chain', 'sin(2X),2X', ''), ('K out', '', 'cos(2X)'), ('K in', '2X', '2'), ('K mul', 'cos(2X),2', '2cos(2X)'), ('K keep', '3,2cos(2X)', '6cos(2X)')]),
    ('DERIV(e^(2X),X)', '2e^(2X)', [('K chain', 'e^(2X),2X', ''), ('K out', '', 'e^(2X)'), ('K in', '2X', '2'), ('K mul', 'e^(2X),2', '2e^(2X)')]),
    ('DERIV(sin(X)^2,X)', '2sin(X)cos(X)', [('K chain', 'sin(X)²,sin(X)', ''), ('K out', '', '2sin(X)'), ('K in', 'sin(X)', 'cos(X)'), ('K mul', '2sin(X),cos(X)', '2sin(X)cos(X)')]),
    ('DERIV(ln(2X+1),X)', '2/(2X+1)', [('K chain', 'ln(2X+1),2X+1', ''), ('K out', '', '1/(2X+1)'), ('K in', '2X+1', '2'), ('K mul', '1/(2X+1),2', '2/(2X+1)')]),
    ('DERIV(1/X,X)', '-1/X²', [('K quot', '1,X', ''), ('K const', '1', '0'), ('K x', 'X', '1'), ('K qput', '', '-1/X²')]),
    ('DERIV(5,X)', '0', [('K const', '5', '0')]),
    ('DERIV(sin(cos(X)),X)', '-sin(X)cos(cos(X))', [('K chain', 'sin(cos(X)),cos(X)', ''), ('K out', '', 'cos(cos(X))'), ('K in', 'cos(X)', '-sin(X)'), ('K mul', 'cos(cos(X)),-sin(X)', '-sin(X)cos(cos(X))')]),
    ('DERIV(e^(sin(X)),X)', 'e^(sin(X))cos(X)', [('K chain', 'e^(sin(X)),sin(X)', ''), ('K out', '', 'e^(sin(X))'), ('K in', 'sin(X)', 'cos(X)'), ('K mul', 'e^(sin(X)),cos(X)', 'e^(sin(X))cos(X)')]),
    ('DERIV(ln(sin(X)),X)', 'cos(X)/sin(X)', [('K chain', 'ln(sin(X)),sin(X)', ''), ('K out', '', '1/sin(X)'), ('K in', 'sin(X)', 'cos(X)'), ('K mul', '1/sin(X),cos(X)', 'cos(X)/sin(X)')]),
    ('DERIV(tan⁻¹(2X),X)', '2/(4X²+1)', [('K chain', 'tan⁻¹(2X),2X', ''), ('K out', '', '1/(4X²+1)'), ('K in', '2X', '2'), ('K mul', '1/(4X²+1),2', '2/(4X²+1)')]),
    ('DERIV(sin⁻¹(X),X)', '-√(1-X²)/(X²-1)', [('K asin', 'sin⁻¹(X)', '-√(1-X²)/(X²-1)')]),
    ('DERIV(cos⁻¹(X),X)', '√(1-X²)/(X²-1)', [('K acos', 'cos⁻¹(X)', '√(1-X²)/(X²-1)')]),
    ('DERIV(2^X,X)', '2^Xln(2)', [('K powb', '2^X', '2^Xln(2)')]),
    ('DERIV(10^X,X)', '10^Xln(10)', [('K powb', '10^X', '10^Xln(10)')]),
    ('DERIV(X^(1/3),X)', '1/(3X^(2/3))', [('K powc', 'X^(1/3)', '1/(3X^(2/3))')]),
    ('DERIV(X^π,X)', 'πX^(π-1)', [('K powc', 'X^(π)', 'πX^(π-1)')]),
    ('DERIV(X^X,X)', 'X^Xln(X)+X^X', [('K powl', 'X^X', 'X^Xln(X)+X^X')]),
    ('DERIV(Xtan⁻¹(X),X)', 'X/(X²+1)+tan⁻¹(X)', [('K prod', 'X,tan⁻¹(X)', ''), ('K x', 'X', '1'), ('K atan', 'tan⁻¹(X)', '1/(X²+1)'), ('K put', '', 'tan⁻¹(X)+X(1/(X²+1))'), ('K simp', '', 'X/(X²+1)+tan⁻¹(X)')]),
    ('DERIV(X*e^(X),X)', 'Xe^(X)+e^(X)', [('K prod', 'X,e^(X)', ''), ('K x', 'X', '1'), ('K exp', 'e^(X)', 'e^(X)'), ('K put', '', 'e^(X)+Xe^(X)'), ('K simp', '', 'Xe^(X)+e^(X)')]),
    ('DERIVAT(X^3+2X,X,2)', '14', [('K sum', '', ''), ('K pow', 'X³', '3X²'), ('K cx', '2X', '2'), ('K add', '', '3X²+2'), ('K at', 'X=2', '14')]),
    ('DERIV(sin(X)/X,X)', 'cos(X)/X-sin(X)/X²', [('K quot', 'sin(X),X', ''), ('K sin', 'sin(X)', 'cos(X)'), ('K x', 'X', '1'), ('K qput', '', '(Xcos(X)-sin(X))/X²'), ('K simp', '', 'cos(X)/X-sin(X)/X²')]),
    ('DERIV(√X),X)', '√(X)/(2X)', [('K root', '√(X)', '√(X)/(2X)')]),
    ('DERIV(A*X^2,X)', '2AX', [('K pow', 'X²', '2X'), ('K keep', 'A,2X', '2AX')]),
    ('INTEGRAL(cos(2X),X)', 'sin(2X)/2', [('I lin', 'cos(2X),2', 'sin(2X)/2'), ('I c', '', 'sin(2X)/2+C')]),
    ('INTEGRAL(sin(X),X,0,π)', '2', [('I sin', 'sin(X)', '-cos(X)'), ('I fb', 'π', '-cos(π)=1'), ('I fa', '0', '-cos(0)=-1'), ('I fd', '0,π', '1-(-1)=2')]),
    ('INTEGRAL(cos(X),X,0,π/2)', '1', [('I cos', 'cos(X)', 'sin(X)'), ('I fb', 'π/2', 'sin((π/2))=1'), ('I fa', '0', 'sin(0)=0'), ('I fd', '0,π/2', '1-0=1')]),
    ('INTEGRAL(1/X,X,1,e)', '1', [('I pfr', '', '1/X'), ('I pfi', '', 'ln(abs(X))'), ('I fb', 'e', 'ln(abs((e)))=1'), ('I fa', '1', 'ln(abs(1))=0'), ('I fd', '1,e', '1-0=1')]),
    ('INTEGRAL(1/(X^2+1),X,0,1)', 'π/4', [('I pfr', '', '1/(X²+1)'), ('I atan', '', 'tan⁻¹(X)'), ('I pfi', '', 'tan⁻¹(X)'), ('I fb', '1', 'tan⁻¹(1)=π/4'), ('I fa', '0', 'tan⁻¹(0)=0'), ('I fd', '0,1', 'π/4-0=π/4')]),
    ('INTEGRAL(1/(X^2+4),X)', 'tan⁻¹(X/2)/2', [('I pfr', '', '1/(X²+4)'), ('I atan', '', 'tan⁻¹(X/2)/2'), ('I pfi', '', 'tan⁻¹(X/2)/2'), ('I c', '', 'tan⁻¹(X/2)/2+C')]),
    ('INTEGRAL((X+1)/(X^2+1),X)', 'ln(X²+1)/2+tan⁻¹(X)', [('I pfr', '', '(X+1)/(X²+1)'), ('I atan', '', 'tan⁻¹(X)'), ('I pfi', '', 'ln(X²+1)/2+tan⁻¹(X)'), ('I c', '', 'ln(X²+1)/2+tan⁻¹(X)+C')]),
    ('INTEGRAL(1/√4-X^2),X)', 'sin⁻¹(X/2)', [('I asin', '', 'sin⁻¹(X/2)'), ('I c', '', 'sin⁻¹(X/2)+C')]),
    ('INTEGRAL(X/(X^2+1),X)', 'ln(X²+1)/2', [('I usub', 'X²+1,2X', ''), ('I uint', '', '1/(2U)'), ('I any', '1/(2U)', 'ln(U)/2'), ('I uback', 'X²+1', 'ln(X²+1)/2'), ('I c', '', 'ln(X²+1)/2+C')]),
    ('INTEGRAL(X*√X^2+1),X)', 'X²√(X²+1)/3+√(X²+1)/3', [('I usub', 'X²+1,2X', ''), ('I uint', '', '√(U)/2'), ('I any', '√(U)/2', 'U√(U)/3'), ('I uback', 'X²+1', 'X²√(X²+1)/3+√(X²+1)/3'), ('I c', '', 'X²√(X²+1)/3+√(X²+1)/3+C')]),
    ('INTEGRAL(cos(X)/(1+sin(X)),X)', 'ln(abs(sin(X)+1))', [('I usub', 'sin(X),cos(X)', ''), ('I uint', '', '1/(U+1)'), ('I any', '1/(U+1)', 'ln(abs(U+1))'), ('I uback', 'sin(X)', 'ln(abs(sin(X)+1))'), ('I c', '', 'ln(abs(sin(X)+1))+C')]),
    ('INTEGRAL(X^(1/3),X)', '3X^(4/3)/4', [('I any', 'X^(1/3)', '3X^(4/3)/4'), ('I c', '', '3X^(4/3)/4+C')]),
    ('INTEGRAL(2^X,X)', '2^X/ln(2)', [('I any', '2^X', '2^X/ln(2)'), ('I c', '', '2^X/ln(2)+C')]),
    ('LIMIT(X^2+1,X,2)', '5', [('L dir', '2', '2²+1=5')]),
    ('LIMIT((X^2-1)/(X-1),X,1)', '2', [('L zero', 'X=1', '0/0'), ('L cancel', '(X-1)', 'X+1'), ('L dir', '1', '1+1=2')]),
    ('LIMIT(sin(X)/X,X,0)', '1', [('L zero', 'X=0', '0/0'), ('L series', 'X=0,X,X', ''), ('L same', '', '1')]),
    ('LIMIT((1-cos(X))/X^2,X,0)', '1/2', [('L zero', 'X=0', '0/0'), ('L series', 'X=0,X²/2,X²', ''), ('L same', '', '1/2')]),
    ('LIMIT(X^2/sin(X),X,0)', '0', [('L zero', 'X=0', '0/0'), ('L series', 'X=0,X²,X', ''), ('L top smaller', '', '0')]),
    ('LIMIT((3X^2+1)/(X^2-5),X,ᴇ99)', '3', [('L inf', '3X²,X²', ''), ('L inf same', '', '3')]),
    ('LIMIT((2X+1)/(X^2+1),X,ᴇ99)', '0', [('L inf', '2X,X²', ''), ('L inf zero', '', '0')]),
    ('DOMINANTTERM(sin(X)/X^2,X)', '1/X', [('L series', 'X=0,X,X²', ''), ('L dominant', '', '1/X')]),
    ('TANGENTLINE(X^2,X,1)', '2X-1', [('T deriv', '', '2X'), ('T slope', '1', '2(1)=2'), ('T height', '1', '1²=1'), ('T line', '', '1+2(X-1)'), ('T simp', '', '2X-1')]),
    ('NORMALLINE(X^2,X,1)', '3/2-X/2', [('T deriv', '', '2X'), ('T slope', '1', '2(1)=2'), ('T height', '1', '1²=1'), ('T nslope', '', '-1/2'), ('T line', '', '1-(1/2)(X-1)'), ('T simp', '', '3/2-X/2')]),
    ('CENTRALDIFF(X^3,X,H)', 'H²+3X²', [('C plus', '', 'H³+3H²X+3HX²+X³'), ('C minus', '', 'X³-3HX²+3H²X-H³'), ('C diff', '', 'H²+3X²')]),
    ('TAYLOR(sin(X),X,3)', 'X-X³/6', [('Y term', '0,0,0', '0'), ('Y term', '1,0,1', 'X'), ('Y term', '2,0,0', '0'), ('Y term', '3,0,-1', '-X³/6'), ('Y sum', '', 'X-X³/6')]),
    ('TAYLOR(e^(X),X,3,0)', 'X³/6+X²/2+X+1', [('Y term', '0,0,1', '1'), ('Y term', '1,0,1', 'X'), ('Y term', '2,0,1', 'X²/2'), ('Y term', '3,0,1', 'X³/6'), ('Y sum', '', 'X³/6+X²/2+X+1')]),
    ('SERIES(1/sin(X),X,3)', '7X³/360+X/6+1/X', [('S top', '', '1'), ('S bottom', '', 'X^5/120-X³/6+X'), ('S divide', '', '7X³/360+X/6+1/X')]),
    ('SERIES(e^(X),X,3)', 'X³/6+X²/2+X+1', [('S series', '', 'X³/6+X²/2+X+1')]),
    ('IMPDIF(X^2+Y^2=25,X,Y)', '-X/Y', [('M side', '', 'X²+Y²-25=0'), ('M dx', '', '2X'), ('M dy', '', '2Y'), ('M slope', '', '-X/Y')]),
    ('IMPDIF(X^2+Y^2=1,X,Y,2)', '(-X²-Y²)/Y³', [('M side', '', 'X²+Y²-1=0'), ('M dx', '', '2X'), ('M dy', '', '2Y'), ('M slope', '', '-X/Y'), ('M next', '', '(-X²-Y²)/Y³')]),
    ('SUM(X^2,X,1,4)', '30', [('U list', '', '1²+2²+3²+4²'), ('U values', '', '1+4+9+16'), ('U sum', '', '30')]),
    ('SUM(X^2,X,1,N)', 'N³/3+N²/2+N/6', [('U formula', '', 'N³/3+N²/2+N/6')]),
    ('SUM(1/X,X,1,4)', '25/12', [('U list', '', '1/1+1/2+1/3+1/4'), ('U values', '', '1+1/2+1/3+1/4'), ('U sum', '', '25/12')]),
    ('PRODUCT(X+1,X,1,4)', '120', [('P list', '', '(1+1)*(2+1)*(3+1)*(4+1)'), ('P values', '', '2*3*4*5'), ('P product', '', '120')]),
    ('FMIN(X^2-2X,X)', 'X=1', [('F deriv', '', '2X-2'), ('F root', '1', '1²-2(1)=-1'), ('F min', '', 'X=1')]),
    ('FMAX(X^3-3X,X,~2,2)', 'X=-1 or X=2', [('F deriv', '', '3X²-3'), ('F root', '-1', '(-1)³-3(-1)=2'), ('F root', '1', '1³-3(1)=-2'), ('F end', '-2', '(-2)³-3(-2)=-2'), ('F end', '2', '2³-3(2)=2'), ('F max', '', 'X=-1 or X=2')]),
    ('ARCLEN(X,X,0,1)', '√(2)', [('A deriv', '', '1'), ('A formula', '', '√(1+1²)'), ('A int', '0,1', '√(2)')]),
    ('ARCLEN(X^2/4-ln(X)/2,X,1,2)', 'ln(2)/2+3/4', [('A deriv', '', '(X²-1)/(2X)'), ('A formula', '', '√(1+((X²-1)/(2X))²)'), ('A root', '', '(X²+1)/(2X)'), ('A int', '1,2', 'ln(2)/2+3/4')]),
    ("DESOLVE(Y'=Y^2&Y(0)=1,X,Y)", 'Y=-1/(X-1)', [('D form', '', "Y'=Y²"), ('D separate', '', '-1/Y=X+C'), ('D start', 'Y(0)=1', '-1=C'), ('D const', '', 'C=-1'), ('D solvey', '', 'Y=-1/(X-1)')]),
    ("DESOLVE(Y''-3Y'+2Y=0,X,Y)", 'Y=Ce^(2X)+De^(X)', [('D char', '', 'R²-3R+2=0'), ('D root', '', 'R=2'), ('D root', '', 'R=1'), ('D homog', '', 'Y=Ce^(2X)+De^(X)')]),
    ("DESOLVE(Y''+Y=X&Y(0)=1&Y'(0)=0,X,Y)", 'Y=X+cos(X)-sin(X)', [('D char', '', 'R²+1=0'), ('D complex', '', 'R=i or R=-i'), ('D homog', '', 'Y=Ccos(X)+Dsin(X)'), ('D particular', '', 'Y=X'), ('D add', '', 'Y=Ccos(X)+Dsin(X)+X'), ('D start', 'Y(0)=1', 'C=1'), ('D start', "Y'(0)=0", 'D=-1'), ('D const', '', 'C=1'), ('D const', '', 'D=-1')]),
    ("DESOLVE(Y'=2Y&Y(0)=3,X,Y)", 'Y=3e^(2X)', [('D form', '', "Y'=2Y"), ('D homog', '', 'Y=Ce^(2X)'), ('D start', 'Y(0)=3', 'C=3'), ('D const', '', 'C=3')]),
    ("DESOLVE(Y'+Y/X=X,X,Y)", 'Y=(X³+3C)/(3X)', [('D form', '', "Y'=(X²-Y)/X"), ('D lp', '', "Y'+(1/X)Y=X"), ('D factor', '', 'X'), ('D multiply', '', "(XY)'=X²"), ('D int', '', 'XY=X³/3+C'), ('Divide both sides by', 'X', 'Y=C/X+X²/3')]),
    ("DESOLVE(Y'+2XY=X,X,Y)", 'Y=Ce^(-X²)+1/2', [('D form', '', "Y'=X-2XY"), ('D lp', '', "Y'+(2X)Y=X"), ('D factor', '', 'e^(X²)'), ('D multiply', '', "(e^(X²)Y)'=Xe^(X²)"), ('D int', '', 'e^(X²)Y=e^(X²)/2+C'), ('Divide both sides by', 'e^(X²)', 'Y=Ce^(-X²)+1/2')]),
    ("DESOLVE(Y'=1+Y^2,X,Y)", 'Y=tan(X+C)', [('D form', '', "Y'=Y²+1"), ('D separate', '', '2tan⁻¹(Y)=2X+C'), ('D inv', '', 'Y=tan(X+C)')]),
    ("DESOLVE(XY'=2Y,X,Y)", 'Y=CX²', [('D form', '', "Y'=2Y/X"), ('D lp', '', "Y'+(-2/X)Y=0"), ('D factor', '', '1/X²'), ('D multiply', '', "((1/X²)Y)'=0"), ('D int', '', '(1/X²)Y=C'), ('Divide both sides by', '1/X²', 'Y=CX²')]),
    ("DESOLVE(Y'=Y(1-Y),X,Y)", 'Y=1/(Ce^(-X)+1)', [('D form', '', "Y'=Y-Y²"), ('D separate', '', '2ln(abs(Y-1))-2ln(abs(Y))=-2X+C'), ('D exp', '', '(Y-1)/Y=Ce^(-X)'), ('D solvey', '', 'Y=1/(Ce^(-X)+1)')]),
    ("DESOLVE(Y'=2X,X,Y)", 'Y=X²+C', [('D form', '', "Y'=2X"), ('D integrate', '', 'Y=X²+C')]),
    ("DESOLVE(Y'+Y=X,X,Y)", 'Y=Ce^(-X)+X-1', [('D form', '', "Y'=X-Y"), ('D homog', '', 'Y=Ce^(-X)'), ('D particular', '', 'Y=X-1'), ('D add', '', 'Y=Ce^(-X)+X-1')]),
    ("DESOLVE(Y'=X/Y&Y(0)=2,X,Y)", 'Y=√(X²+4)', [('D form', '', "Y'=X/Y"), ('D separate', '', 'Y²/2=X²/2+C'), ('D start', 'Y(0)=2', '2=C'), ('D const', '', 'C=2'), ('D solvey', '', 'Y=√(X²+4)')]),
    ("DESOLVE(Y''-2Y'+Y=X,X,Y)", 'Y=DXe^(X)+Ce^(X)+X+2', [('D char', '', 'R²-2R+1=0'), ('D repeat', '', 'R=1'), ('D homog', '', 'Y=Ce^(X)+DXe^(X)'), ('D particular', '', 'Y=X+2'), ('D add', '', 'Y=Ce^(X)+DXe^(X)+X+2')]),
    ("DESOLVE(Y''+2Y'+5Y=0&Y(0)=1&Y'(0)=0,X,Y)", 'e^(-X)(cos(2X)+sin(2X)/2)', [('D char', '', 'R²+2R+5=0'), ('D complex', '', 'R=-1+2i or R=-1-2i'), ('D homog', '', 'Y=Ce^(-X)cos(2X)+De^(-X)sin(2X)'), ('D start', 'Y(0)=1', 'C=1'), ('D start', "Y'(0)=0", '2D-C=0'), ('D const', '', 'C=1'), ('D const', '', 'D=1/2')]),
    ("DESOLVE(Y''-3Y'+2Y=0&Y(0)=1&Y'(0)=0,X,Y)", 'Y=2e^(X)-e^(2X)', [('D char', '', 'R²-3R+2=0'), ('D root', '', 'R=2'), ('D root', '', 'R=1'), ('D homog', '', 'Y=Ce^(2X)+De^(X)'), ('D start', 'Y(0)=1', 'C+D=1'), ('D start', "Y'(0)=0", '2C+D=0'), ('D const', '', 'C=-1'), ('D const', '', 'D=2')]),
    ("DESOLVE(Y''-2Y'+Y=0&Y(0)=1&Y'(0)=0,X,Y)", 'Y=e^(X)(1-X)', [('D char', '', 'R²-2R+1=0'), ('D repeat', '', 'R=1'), ('D homog', '', 'Y=Ce^(X)+DXe^(X)'), ('D start', 'Y(0)=1', 'C=1'), ('D start', "Y'(0)=0", 'C+D=0'), ('D const', '', 'C=1'), ('D const', '', 'D=-1')]),
    ('CSOLVE(X²+2X+5=0,X)', 'X=-1-2i or X=-1+2i', [('Quadratic formula', '1,2,5', ''), ('Discriminant value', '', '2^2-4*1*5=-16'), ('Put in the formula', '', 'X=(-2-√(-16))/2 or X=(-2+√(-16))/2'), ('Use i', '', '√(-16)=4i'), ('Simplify roots', '', 'X=-1-2i or X=-1+2i')]),
    ('SOLVE(X²+5X+6=0,X)', 'X=-3 or X=-2', [('Find two numbers', '6,5,2,3', ''), ('Factor the left side', '', '(X+2)(X+3)=0'), ('Set to 0', 'X+3', 'X=-3'), ('Set to 0', 'X+2', 'X=-2')]),
    ('SOLVE(X³-X=0,X)', 'X=-1 or X=0 or X=1', [('Common factor', '', 'X(X²-1)'), ('Find two numbers', '-1,0,-1,1', ''), ('Factor the left side', '', 'X(X-1)(X+1)=0'), ('Set to 0', 'X+1', 'X=-1'), ('Set to 0', 'X', 'X=0'), ('Set to 0', 'X-1', 'X=1')]),
    # end stepscalc golden
    ('FACTOR(X^2-Y^2)', '(X-Y)(X+Y)', [('Difference of squares', 'X,Y', '(X-Y)(X+Y)')]),
    ('FACTOR(X^2+2XY+Y^2)', '(X+Y)²', [('Perfect square', 'X,Y', '(X+Y)²')]),
    ('FACTOR(X^4+4)', '(X²-2X+2)(X²+2X+2)', [('Add and subtract', '4X²', '(X²+2)²-(2X)²'), ('Difference of squares', 'X²+2,2X', '(X²-2X+2)(X²+2X+2)')]),
    ('FACTOR(X^4+X^2+1)', '(X²-X+1)(X²+X+1)', [('Add and subtract', 'X²', '(X²+1)²-X²'), ('Difference of squares', 'X²+1,X', '(X²-X+1)(X²+X+1)')]),
    ('LIMIT(ln(1+X)/X,X,0)', '1', [('L zero', 'X=0', '0/0'), ('L series', 'X=0,X,X', ''), ('L same', '', '1')]),
    ('LIMIT((X-1)/ln(X),X,1)', '1', [('L zero', 'X=1', '0/0'), ('L series', 'X=1,(X-1),(X-1)', ''), ('L same', '', '1')]),
    ('INTEGRAL(1/√X),X)', '2√(X)', [('Write', '1/√(X)', 'X^(-1/2)'), ('I pow', 'X^(-1/2),1/2', '2√(X)'), ('I c', '', '2√(X)+C')]),
    ('INTEGRAL(sin(X)^2cos(X),X)', 'sin(X)/4-sin(3X)/12', [('I usub', 'sin(X),cos(X)', ''), ('I uint', '', 'U²'), ('I pow', 'U²,3', 'U³/3'), ('I uback', 'sin(X)', 'sin(X)³/3'), ('Same answer, written', '', 'sin(X)/4-sin(3X)/12'), ('I c', '', 'sin(X)/4-sin(3X)/12+C')]),
    ('EXPAND((X+1)^5)', 'X^5+5X^4+10X³+10X²+5X+1', [("Pascal's triangle", '5,X,1', '1,5,10,10,5,1'), ('Write the terms', '', 'X^5+5*X^4+10*X^3+10*X^2+5*X+1'), ('Combine like terms', '', 'X^5+5X^4+10X³+10X²+5X+1')]),
    ('EXPAND((X-2)^5)', 'X^5-10X^4+40X³-80X²+80X-32', [("Pascal's triangle", '5,X,-2', '1,5,10,10,5,1'), ('Write the terms', '', 'X^5+5*X^4*(-2)+10*X^3*(-2)^2+10*X^2*(-2)^3+5*X*(-2)^4+(-2)^5'), ('Combine like terms', '', 'X^5-10X^4+40X³-80X²+80X-32')]),
    # stepsfx golden
    ('SOLVE(2^X=1/3,X)', 'X=-ln(3)/ln(2)', [('X let', '2^X', 'U=1/3'), ('X back', '2^X', '2^X=1/3'), ('X ln down', '', 'Xln(2)=-ln(3)'), ('Divide both sides by', 'ln(2)', 'X=-ln(3)/ln(2)')]),
    ('SOLVE(e^(X)=1/3,X)', 'X=-ln(3)', [('X let', 'e^(X)', 'U=1/3'), ('X back', 'e^(X)', 'e^(X)=1/3'), ('X ln', '', 'X=-ln(3)')]),
    ('SOLVE(e^(X)=5,X)', 'X=ln(5)', [('X let', 'e^(X)', 'U=5'), ('X back', 'e^(X)', 'e^(X)=5'), ('X ln', '', 'X=ln(5)')]),
    ('SOLVE(2^X=8,X)', 'X=3', [('X let', '2^X', 'U=8'), ('X back', '2^X', '2^X=8'), ('X ln down', '', 'Xln(2)=ln(8)'), ('Divide both sides by', 'ln(2)', 'X=ln(8)/ln(2)'), ('X simplify', '8,2,3', 'X=3')]),
    ('SOLVE(3e^(X)=9,X)', 'X=ln(3)', [('X let', 'e^(X)', '3U=9'), ('Divide both sides by', '3', 'U=3'), ('X back', 'e^(X)', 'e^(X)=3'), ('X ln', '', 'X=ln(3)')]),
    ('SOLVE(ln(X)=3,X)', 'X=e^(3)', [('X raise', 'e', 'X=e^(3)')]),
    ('SOLVE(log(X)=2,X)', 'X=100', [('X raise', '10', 'X=100')]),
    ('SOLVE(ln(2X+1)=ln(5),X)', 'X=2', [('X same', '', '2X+1=5'), ('Subtract', '1', '2X=4'), ('Divide both sides by', '2', 'X=2')]),
    ('SOLVE(e^(2X)-3e^(X)+2=0,X)', 'X=0 or X=ln(2)', [('X let', 'e^(X)', 'U²-3U+2=0'), ('Find two numbers', '2,-3,-2,-1', ''), ('Factor the left side', '', '(U-2)(U-1)=0'), ('Set to 0', 'U-1', 'U=1'), ('Set to 0', 'U-2', 'U=2'), ('X back', 'e^(X)', 'e^(X)=1'), ('X ln', '', 'X=0'), ('X back', 'e^(X)', 'e^(X)=2'), ('X ln', '', 'X=ln(2)')]),
    ('SOLVE(1000(1.05)^X=2000,X)', 'X=ln(2)/ln(1.05)', [('X let', '1.05^X', '1000U=2000'), ('Divide both sides by', '1000', 'U=2'), ('X back', '1.05^X', '1.05^X=2'), ('X ln down', '', 'Xln(1.05)=ln(2)'), ('Divide both sides by', 'ln(1.05)', 'X=ln(2)/ln(1.05)')]),
    ('SOLVE(5e^(0.3X)=40,X)', 'X=10ln(8)/3', [('X let', 'e^(0.3X)', '5U=40'), ('Divide both sides by', '5', 'U=8'), ('X back', 'e^(0.3X)', 'e^(0.3X)=8'), ('X ln', '', '0.3X=ln(8)'), ('Multiply both sides by', '10/3', 'X=10ln(8)/3')]),
    ('SOLVE(4^X=2^(X+3),X)', 'X=3', [('X let', '2^X', 'U²=8U'), ('Move everything', '', 'U²-8U=0'), ('Factor the left side', '', 'U(U-8)=0'), ('Set to 0', 'U', 'U=0'), ('Set to 0', 'U-8', 'U=8'), ('X no', '0', ''), ('X back', '2^X', '2^X=8'), ('X ln down', '', 'Xln(2)=ln(8)'), ('Divide both sides by', 'ln(2)', 'X=ln(8)/ln(2)'), ('X simplify', '8,2,3', 'X=3')]),
    ('SOLVE(3^(X+1)=9,X)', 'X=1', [('X let', '3^X', '3U=9'), ('Divide both sides by', '3', 'U=3'), ('X back', '3^X', '3^X=3'), ('X ln down', '', 'Xln(3)=ln(3)'), ('Divide both sides by', 'ln(3)', 'X=1'), ('X simplify', '3,3,1', 'X=1')]),
    ('SOLVE(2^(X-1)=5,X)', 'X=ln(10)/ln(2)', [('X let', '2^X', 'U/2=5'), ('Multiply both sides by', '2', 'U=10'), ('X back', '2^X', '2^X=10'), ('X ln down', '', 'Xln(2)=ln(10)'), ('Divide both sides by', 'ln(2)', 'X=ln(10)/ln(2)')]),
    ('SOLVE(9^X=3^(X+2),X)', 'X=2', [('X let', '3^X', 'U²=9U'), ('Move everything', '', 'U²-9U=0'), ('Factor the left side', '', 'U(U-9)=0'), ('Set to 0', 'U', 'U=0'), ('Set to 0', 'U-9', 'U=9'), ('X no', '0', ''), ('X back', '3^X', '3^X=9'), ('X ln down', '', 'Xln(3)=ln(9)'), ('Divide both sides by', 'ln(3)', 'X=ln(9)/ln(3)'), ('X simplify', '9,3,2', 'X=2')]),
    ('SOLVE(2^X=4^(X+1),X)', 'X=-2', [('X let', '2^X', 'U=4U²'), ('Move everything', '', 'U-4U²=0'), ('Divide both sides by', '-1', '4U²-U=0'), ('Factor the left side', '', 'U(4U-1)=0'), ('Set to 0', 'U', 'U=0'), ('Set to 0', '4U-1', 'U=1/4'), ('X no', '0', ''), ('X back', '2^X', '2^X=1/4'), ('X ln down', '', 'Xln(2)=-ln(4)'), ('Divide both sides by', 'ln(2)', 'X=-ln(4)/ln(2)'), ('X simplify', '1/4,2,-2', 'X=-2')]),
    ('SOLVE(2^(X+1)=3^X,X)', 'X=-ln(2)/(ln(2)-ln(3))', [('X ln both', '', 'Xln(2)+ln(2)=Xln(3)'), ('Subtract', 'Xln(3)', 'Xln(2)-Xln(3)+ln(2)=0'), ('Subtract', 'ln(2)', 'Xln(2)-Xln(3)=-ln(2)'), ('Divide both sides by', 'ln(2)-ln(3)', 'X=-ln(2)/(ln(2)-ln(3))')]),
    ('SOLVE(5^(2X)=2^(X+3),X)', 'X=3ln(2)/(2ln(5)-ln(2))', [('X ln both', '', '2Xln(5)=Xln(2)+3ln(2)'), ('Subtract', 'Xln(2)', '2Xln(5)-Xln(2)=3ln(2)'), ('Divide both sides by', '2ln(5)-ln(2)', 'X=3ln(2)/(2ln(5)-ln(2))')]),
    ('SOLVE(2^(1-X)=3^X,X)', 'X=ln(2)/(ln(2)+ln(3))', [('X ln both', '', 'ln(2)-Xln(2)=Xln(3)'), ('Subtract', 'Xln(3)', 'ln(2)-Xln(3)-Xln(2)=0'), ('Subtract', 'ln(2)', '-Xln(2)-Xln(3)=-ln(2)'), ('Divide both sides by', '-ln(2)-ln(3)', 'X=ln(2)/(ln(2)+ln(3))')]),
    ('SOLVE(ln(X^2)=4,X)', 'X=e^(2) or X=-e^(2)', [('X raise', 'e', 'X²=e^(4)'), ('X root', '', 'X=e^(2) or X=-e^(2)')]),
    ('SOLVE(ln(X^2)=0,X)', 'X=1 or X=-1', [('X raise', 'e', 'X²=1'), ('X root', '', 'X=1 or X=-1')]),
    ('SOLVE(2ln(X)=4,X)', 'X=e^(2)', [('Divide both sides by', '2', 'ln(X)=2'), ('X raise', 'e', 'X=e^(2)')]),
    ('SOLVE(log(X)+log(X-3)=1,X)', 'X=5', [('X combine', '', 'log(X²-3X)=1'), ('X raise', '10', 'X²-3X=10'), ('Move everything', '', 'X²-3X-10=0'), ('Find two numbers', '-10,-3,-5,2', ''), ('Factor the left side', '', '(X-5)(X+2)=0'), ('Set to 0', 'X+2', 'X=-2'), ('Set to 0', 'X-5', 'X=5'), ('X domain', '-2', '')]),
    ('SOLVE(ln(X)+ln(2)=3,X)', 'X=e^(3)/2', [('X combine', '', 'ln(2X)=3'), ('X raise', 'e', '2X=e^(3)'), ('Divide both sides by', '2', 'X=e^(3)/2')]),
    ('SOLVE(ln(2X+1)=3,X)', 'X=(e^(3)-1)/2', [('X raise', 'e', '2X+1=e^(3)'), ('Subtract', '1', '2X=e^(3)-1'), ('Divide both sides by', '2', 'X=(e^(3)-1)/2')]),
    ('SOLVE(2sin(X)=1,X)', 'X=π/6+2πN or X=5π/6+2πN', [('Divide both sides by', '2', 'sin(X)=1/2'), ('T in sin', '', 'X=π/6'), ('T all sin', '', 'X=π/6 or X=5π/6'), ('T period', '', 'X=π/6+2πN or X=5π/6+2πN')]),
    ('SOLVE(2sin(2X+π/3)=1,X)', 'X=-π/12+πN or X=π/4+πN', [('Divide both sides by', '2', 'sin(2X+π/3)=1/2'), ('T in sin', '', '2X+π/3=π/6'), ('T all sin', '', '2X+π/3=π/6 or 2X+π/3=5π/6'), ('T period', '', '2X+π/3=π/6+2πN or 2X+π/3=5π/6+2πN'), ('T solve', '', 'X=-π/12+πN or X=π/4+πN')]),
    ('SOLVE(tan(X)=1,X,0,3π)', 'X=π/4 or X=5π/4 or X=9π/4', [('T in tan', '', 'X=π/4'), ('T period', '', 'X=π/4+πN'), ('T bounds', '0,3π', 'X=π/4 or X=5π/4 or X=9π/4')]),
    ('SOLVE(cos(X)=~1/2,X)', 'X=2π/3+2πN or X=4π/3+2πN', [('T in cos', '', 'X=2π/3'), ('T all cos', '', 'X=2π/3 or X=4π/3'), ('T period', '', 'X=2π/3+2πN or X=4π/3+2πN')]),
    ('LIMIT((√X+4)-2)/X,X,0)', '1/4', [('L conj', '(√(X+4)+2)', 'X/(X(√(X+4)+2))'), ('L answer', '', '1/4')]),
    ('LIMIT(√X^2+X)-X,X,ᴇ99)', '1/2', [('L conj', '(X+√(X²+X))', 'X/(X+√(X²+X))'), ('L inf', '1/2,1', ''), ('L inf same', '', '1/2')]),
    ('LIMIT(abs(X)/X,X,0,1)', '1', [('L abs', '1,X', ''), ('L answer', '', '1')]),
    ('LIMIT(abs(X-1)/(X-1),X,1,~1)', '-1', [('L abs', '-1,1-X', ''), ('L answer', '', '-1')]),
    ('LIMIT(X*sin(1/X),X,0)', '0', [('L grow', 'Xsin(1/X)', ''), ('L squeeze', '', '0')]),
    ('LIMIT(X^2*e^(~X),X,ᴇ99)', '0', [('L grow', 'X²e^(-X)', ''), ('L zero', '', '0')]),
    ('LIMIT(ln(X)/X,X,ᴇ99)', '0', [('L grow', 'ln(X)/X', ''), ('L zero', '', '0')]),
    ('LIMIT(X*ln(X),X,0,1)', '0', [('L grow', 'Xln(X)', ''), ('L zero', '', '0')]),
    ('LIMIT(X^X,X,0,1)', '1', [('L pow', '', 'Xln(X)'), ('L grow', 'Xln(X)', ''), ('L zero', '', '0'), ('L exp', '', '1')]),
    ('SUM(5(0.9)^K,K,0,ᴇ99)', '50', [('S geo', '5,0.9', ''), ('S ginf', '', '50')]),
    ('SUM(2^K,K,0,N)', '2^(N+1)-1', [('S geo', '1,2', ''), ('S gsum', '', '2^(N+1)-1')]),
    ('SUM(1/(K(K+1)),K,1,ᴇ99)', '1', [('S pf', '1/(K²+K)', ''), ('S tel', '', '1')]),
    ('SUM(1/(K(K+2)),K,1,ᴇ99)', '3/4', [('S pf', '1/(K²+2K)', ''), ('S tel', '', '3/4')]),
    ('SUM(1/K^2,K,2,ᴇ99)', 'π²/6-1', [('S ps', '', 'π²/6-1')]),
    ('LIMIT((1+1/X)^X,X,ᴇ99)', 'e', [('L one', '', '1'), ('L exp', '', 'e')]),
    ('LIMIT((1+2X)^(1/X),X,0)', 'e^(2)', [('L one', '', '2'), ('L exp', '', 'e^(2)')]),
    ('LIMIT(tan⁻¹(X),X,ᴇ99)', 'π/2', [('L put', 'tan⁻¹(X),π/2', ''), ('L inf', 'π/2,1', ''), ('L inf same', '', 'π/2')]),
]
NOSTEPS = ['EXPAND(X^2+X)']
# steps(E) answers as E does (nothing about the answer may change), and the last step of a SOLVE or
# DERIV is the answer it explains
CORPUS = ['SOLVE(cos(X)=1/2,X)', 'SOLVE(sin(X)=~1,X)', 'SOLVE(tan(X)=√3),X)', 'SOLVE(sin(X)^2=1,X)', 'SOLVE(e^(X)=5,X)', 'SOLVE(4^X=2^(X+3),X)', 'SOLVE(3^(X+1)=9,X)', 'SOLVE(2^(X-1)=5,X)', 'SOLVE(9^X=3^(X+2),X)', 'SOLVE(2^X=4^(X+1),X)', 'SOLVE(2^(X+1)=3^X,X)', 'SOLVE(5^(2X)=2^(X+3),X)', 'SOLVE(2^(1-X)=3^X,X)', 'SOLVE(ln(X^2)=4,X)', 'SOLVE(ln(X^2)=0,X)', 'SOLVE(2^X=8,X)', 'SOLVE(3e^(X)=9,X)', 'SOLVE(ln(X)=3,X)', 'SOLVE(log(X)=2,X)', 'SOLVE(ln(2X+1)=ln(5),X)', 'SOLVE(e^(2X)-3e^(X)+2=0,X)', 'SOLVE(1000(1.05)^X=2000,X)', 'SOLVE(5e^(0.3X)=40,X)', 'SOLVE(2ln(X)=4,X)', 'SOLVE(ln(X)+ln(2)=3,X)', 'SOLVE(e^(X)=0,X)', 'SOLVE(ln(2X+1)=3,X)', 'SOLVE(2X+3=7,X)', 'SOLVE(0.5X=3,X)', 'SOLVE(2(X+1)=8,X)', 'SOLVE(5=2X+1,X)', 'SOLVE(X=X+1,X)', 'SOLVE(2X=2X,X)',
          'SOLVE(2X+Y=7,X)', 'SOLVE(2X+3=7,Y)', 'DERIV(5X^2/3,X)', 'DERIV(1/X,X)', 'DERIV(sin(2X),X)', 'DERIV(sin(X)cos(X),X)',
          'DERIV(X^4-3X^2+X-9,X)', 'DERIV(X^2*sin(X),X,2)', 'DERIV(ln(X)/X,X)', 'DERIV(5,X)', 'FACTOR(2X^3-8X)', 'EXPAND((X+1)^3)',
          'SOLVE(X^2-2=0,X)', 'SOLVE(X^2+1=0,X)', 'SOLVE(X^4-5X^2+4=0,X)', 'FACTOR(12)', 'FACTOR(X^2-1)', 'FACTOR(-X^2+4)', 'FACTOR(X^2Y-Y)', 'FACTOR(6X^2+X-1)', 'FACTOR(X^4-1)', 'FACTOR(1/2)', 'FACTOR((X^2-1)/(X+1))', 'SOLVE((X^2-1)/(X+1)=0,X)', 'NROOTS(X^3-2X-5)', 'SOLVE(X/2=Y,X)', 'SOLVE(√X+1)=3,X)', 'INTEGRAL(X*sin(X),X)', 'INTEGRAL(√2X+1),X)', 'INTEGRAL(X/(X+1),X)', 'INTEGRAL(X^2,X,0,2)', 'INTEGRAL(sin(X)+X,X,0,2)', 'LIMIT((X^2-1)/(X-1),X,1)', 'LIMIT(sin(X)/X,X,0)', 'LIMIT(X^2/sin(X),X,0)', 'LIMIT((3X^2+1)/(X^2-5),X,ᴇ99)', 'LIMIT(1/X,X,0)', 'LIMIT(sin(X)/X^2,X,0)', 'LIMIT((X^3+1)/(X+1),X,ᴇ99)', 'LIMIT(1/X,X,0,1)', 'DOMINANTTERM(sin(X)/X^2,X)', 'DOMINANTTERM(X/(X^2+X),X,ᴇ99)', 'TANGENTLINE(X^2,X,1)', 'TANGENTLINE(ln(X),X,~1)', 'NORMALLINE(X^2,X,0)', 'NORMALLINE(X^2,X,1)', 'CENTRALDIFF(X^3,X,H)', 'CENTRALDIFF(1/X,X,H)', 'TAYLOR(sin(X),X,5)', 'TAYLOR(ln(X),X,3,1)', 'SERIES(1/sin(X),X,3)', 'SERIES(e^(X)/X,X,2)', 'SERIES(1/X,X,3,1)', 'IMPDIF(X^3+Y^3=6XY,X,Y)', 'IMPDIF(X^2=1,X,Y)', 'SUM(X,X,3,1)', 'SUM(X,X,1,1000)', 'SUM(1/X,X,0,3)', 'SUM(e^(X),X,1,N)', 'PRODUCT(X,X,4,1)', 'PRODUCT(X,X,1,5)', 'FMIN(X^4-4X^2+1,X)', 'FMIN(X^3,X)', 'FMAX(1/(X^2+1),X)', 'ARCLEN(X^2,X,0,1)', 'ARCLEN(2X√X)/3,X,0,3)', 'ARCLEN(X^3,X,0,1)', 'LIMIT((√X+4)-2)/X,X,0)', 'LIMIT(X*sin(1/X),X,0)', 'LIMIT(abs(X)/X,X,0)', 'LIMIT(X^X,X,0,1)', 'LIMIT((1+1/X)^X,X,ᴇ99)', 'LIMIT(X^2*e^(~X),X,ᴇ99)', 'SUM(5(0.9)^K,K,0,ᴇ99)', 'SUM(2^K,K,0,N)', 'SUM(1/(K(K+1)),K,1,ᴇ99)', 'SUM(1/K^2,K,2,ᴇ99)']


def main():
    c = Cli()
    fails = 0
    if len(sys.argv) > 1:
        for e in sys.argv[1:]:
            print(e, c.steps(e))
        return 0

    def check(ok, what):
        nonlocal fails
        print(("ok   " if ok else "FAIL ") + what)
        fails += not ok

    for e, ans, want in GOLDEN:
        a, st, fl = c.steps(e)
        check(a == ans and st == want and not fl, '%s -> %s, %d steps' % (e, ans, len(want)))
        check(c.plain(e) == ans, '%s: the same answer without steps(' % e)
        check(c.logans == ans, '%s: the log ends with the answer' % e)
        check(st[-1][2].split('=', 1)[-1] == ans.split('=', 1)[-1] or st[-1][0] in ('X ln', 'X domain', 'Add the results', 'Quotient rule', 'Set to 0', 'Root', 'Divide by prime', 'No real roots', 'Root fails', 'Top minus bottom', 'Put in', 'Test a number', 'Multiply by the common denominator') or e.startswith('SOLVE({') or e.startswith('DESOLVE') or e.startswith('FACTOR') or e.startswith('INTEGRAL'),
              '%s: the last step is the answer' % e)
    e = "DESOLVE(Y''+Y'+Y=0,X,Y)"          # 31 columns: past the screen, the answer view shows it whole, with steps( too
    a, st, fl = c.steps(e, 'M')
    check(a == c.plain(e, 'M') == "Y=e^(-X/2)(Ccos(\u221a(3)X/2)+Dsin(\u221a(3)X/2))", '%s: the whole answer in MathPrint, with steps( too' % e)
    for e, ans, tags in (("DESOLVE(Y'+Y*tan(X)=sin(X),X,Y)", 'Ccos(X)-cos(X)ln(abs(cos(X)))', ['D form', 'D lp', 'D factor', 'D multiply', 'D int', 'Divide both sides by']),):
        a, st, fl = c.steps(e, 'M')                      # too wide for Classic
        check(a == ans and [r for r, _, _ in st] == tags and a == c.plain(e, 'M'), '%s: its steps in MathPrint' % e)
    a, st, fl = c.steps('SOLVE({Y=X²+1,Y=0},{X,Y})')
    check(a.startswith('E') and st[-2:] == [('Subtract', '1', 'X²=-1'), ('No real roots', '', '')] and c.plain('SOLVE({Y=X²+1,Y=0},{X,Y})') == a, 'a system with no real solution: its steps')
    a, st, fl = c.steps('SOLVE(X^2+1=0,X)')
    check(a.startswith('E') and st == [('Subtract', '1', 'X²=-1'), ('No real roots', '', '')] and c.plain('SOLVE(X^2+1=0,X)') == a and c.logans == '',
          'SOLVE(X^2+1=0,X): the same error, with its steps')
    for e in NOSTEPS:
        a, st, fl = c.steps(e)
        check(st in (None, []) and a == c.plain(e), '%s: no steps, the same answer (%s)' % (e, a))
    for e, rules in (('SOLVE(INTEGRAL(X,X,0,2)=X,X)', ['First', 'Swap']), ('DERIV(INTEGRAL(X,X),X)', ['First', 'K pow', 'K keep'])):
        a, st, fl = c.steps(e)
        check(st and [r for r, _, _ in st] == rules and st[0][2] in ('2', 'X²/2') and a == c.plain(e), '%s: a First step for the inner command' % e)
    for e in ('SOLVE(sin(X)=π/6,X)', 'SOLVE(sin(X)+cos(X)=1,X)', 'SOLVE(sin(X)cos(X)=1/4,X)'):   # out of scope: LIMIT, never SYNTAX
        r = c.run(e)
        check(r.startswith('E') and bytes.fromhex(r[1:]).split(b'\0')[0] == b'SYMCE LIMIT', '%s: SYMCE LIMIT, not SYNTAX' % e)
    for e, ans in (('SOLVE(2sin(X)^2-sin(X)-1=0,X)', 'X=π/2+2πN or X=7π/6+2πN or X=11π/6+2πN'), ('SOLVE(sin(X)=1/3,X)', 'X=sin⁻¹(1/3)+2πN or X=π-sin⁻¹(1/3)+2πN'),
                   ('SOLVE(tan(X)^2=3,X)', 'X=π/3+πN or X=2π/3+πN'), ('SOLVE(sin(X)^2=1,X)', 'X=π/2+πN'), ('SOLVE(sin(X)^2=1/4,X)', 'X=π/6+πN or X=5π/6+πN')):   # steps( shows the whole answer, plain SOLVE is 26 columns
        a, st, fl = c.steps(e)
        check(a == ans and st and not fl and st[-1][2] == ans, '%s: steps( answers %s, the last step is it' % (e, ans))
    a, st, fl = c.steps('SOLVE(2sin(X)=1,X)', 'G')
    check(a == 'X=30+360N or X=150+360N' and st[-1] == ('T period', '', a) and not fl, 'Degree mode: 30+360N')
    a, st, fl = c.steps('SOLVE(sin(X)^2=1,X)', 'G')
    check(a == 'X=90+180N' and not fl, 'Degree mode: two answers half a period apart are one, 90+180N')
    a, st, fl = c.steps('SOLVE(1=8X^3,X)')
    check('Common factor' not in [r for r, _, _ in st], '8X^3-1: no common factor step (it has none)')
    a, st, fl = c.steps('SOLVE(2sin(X)=1,X,0,360)', 'G')
    check(a == 'X=30 or X=150' and st[-1][0] == 'T bounds' and not fl, 'Degree mode, bounds: X=30 or X=150')
    a, st, fl = c.steps('SOLVE(sin(X)=2,X)')
    check(a.startswith('E') and st == [('T range', '', '')] and bytes.fromhex(a[1:]).split(b'\0')[0] == b'NO SOLUTION', 'sin(X)=2: NO SOLUTION, with its step')
    check(c.steps('2+3')[0] == '5' and c.steps('2+3')[1] in (None, []), '2+3 inside steps( is answered, with no steps')
    for e in CORPUS:
        a, st, fl = c.steps(e)
        p = c.plain(e)
        check(a == p or (st is None and p == 'PASS'), '%s: steps( answers %r as it does without' % (e, p))
        if e.startswith('SOLVE(') and st and ' or ' not in a and not a.startswith('E'):        # a SOLVE ends on the solution
            check(st[-1][2] == a, '%s: the last step is %s' % (e, a))
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    eng, men = (open(os.path.join(root, 'symce/src', f)).read() for f in ('engine.c', 'menu.c'))
    n_enum = len(re.findall(r'SR_\w+', re.search(r'enum\s*\{[^}]*SR_SUB[^}]*\}', eng, re.S).group(0)))
    a = men.index('static const char R[] =')
    n_r = len(re.findall(r'\\0"', men[a:men.index('";', a) + 2]))
    check(n_enum == n_r == len(RULES), 'SR_ enum (%d), R[] strings (%d) and RULES (%d) agree' % (n_enum, n_r, len(RULES)))
    # menu.c s_sub fills 150-byte title/why buffers and cuts silently at 149: render every rule with the
    # parameters the engine really logged during this run (SEEN), and with 24/8-char worst cases for the rest
    def cdec(lit):                                      # one C string literal's escapes, each its own character
        return re.sub(r'\\(x[0-9a-fA-F]{1,2}|[0-7]{1,3}|.)', lambda m: chr(int(m.group(1)[1:], 16) if m.group(1)[0] == 'x' and len(m.group(1)) > 1 else int(m.group(1), 8) if m.group(1)[0] in '01234567' else ord(m.group(1))), lit)
    body = re.sub(r'/\*.*?\*/|//[^\n]*', '', men[a:men.rindex('\\0"', a, men.index('";', a) + 2) + 3] + '"', flags=re.S)
    tpl = ''.join(cdec(x) for x in re.findall(r'"((?:[^"\\]|\\.)*)"', body)).split('\0')[:-1]
    check(len(tpl) == n_r == len(RULES), 'every rule parsed for the length check (%d of %d)' % (len(tpl), n_r))

    def pieces(par):                                    # the top-level comma-separated parts of a parameter
        out, d, cur = [], 0, ''
        for ch in par:
            d += ch in '([{' ; d -= ch in ')]}'
            if ch == ',' and not d: out.append(cur); cur = ''
            else: cur += ch
        return out + [cur]

    def render(t, par, worst):
        pc = pieces(par)
        def sub(m):
            if worst: return 'x' * (8 if m.group(1) else 24)
            return pc[int(m.group(1)) - 1] if m.group(1) and int(m.group(1)) <= len(pc) else par if not m.group(1) else ''
        return re.sub(r'%(\d?)', sub, t.replace('\1', '#')).split('\2')
    longest = (0, '')
    for r, t in enumerate(tpl):
        seen = [(q, 0) for rr, q in sorted(SEEN) if rr == r]
        for par, worst in seen or [(None, 1)]:
            for x in render(t.lstrip('='), par or '', worst):
                longest = max(longest, (len(x), x[:40]))
                check(len(x) <= 148, 'rule %d text fits the 149-char step buffer (%d, %s)' % (r, len(x), x[:40]) if len(x) > 148 else 'rule %d fits' % r)
    print('longest rendered step text: %d chars (%s), %d real parameters measured' % (longest[0], longest[1], len(SEEN)))
    print('ALL PASS' if not fails else '%d FAILED' % fails)
    return fails


if __name__ == '__main__':
    sys.exit(main())
