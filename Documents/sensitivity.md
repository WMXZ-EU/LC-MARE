% Hydrophone Sensitivity
% Walter MX Zimmer
% 22-03-2026

Contents:

<!-- TOC -->
* [Reference](#reference)
* [Cylinder](#cylinder)
  * [Lamé equations](#lamé-equations)
* [Electric field](#electric-field)
  * [Note:](#note)
  * [generated voltage](#generated-voltage)
* [Sphere](#sphere)
<!-- TOC -->

## Reference
https://engineeringlibrary.org/reference/thick-pressure-vessels-air-force-stress-manual

https://apps.dtic.mil/sti/pdfs/AD0759199.pdf

## Cylinder
Given a cylider with a,b as internal,external radius stress is given by the  Lamé equations.


### Lamé equations
For cylinder we have the following stress equations

#### radial tension
$$
T_3(r)=A-\frac{B}{r^2}
$$

#### circumferential tension
$$T_1(r)=A+\frac{B}{r^2}
$$

#### axial tension (end-capped cylinder)
$$T_2(r)=A
$$
whereby
$$A=\frac{P_aa^2-P_bb^2}{b^2-a^2}
$$
$$
B=\frac{a^2b^2(P_a-P_b)}{b^2-a^2}
$$

Assume for hydrophones internal pressure $P_a=0$, then the two constants $A$ and $B$ become
$$
A=-P_b\frac{b^2}{b^2-a^2}
$$
$$
B=-P_b\frac{a^2b^2}{b^2-a^2} = Aa^2
$$

and the three stress equations are then
$$
T_3(r)=A(1-\frac{a^2}{r^2})
$$
$$
T_1(r)=A(1+\frac{a^2}{r^2})
$$
$$
T_2(r)=A
$$
with $A$ as defined above

## Electric field
The electric field in radial direction $E_3(r)$ due to external pressure becomes with $g_{32}=g_{31}$
$$
E_3(r)=-g_{31} (T_1(r)+T_2(r))-g_{33} T_3(r)
$$
and the measured voltage $V$
$$
V = \int_a^b{E_3(r)dr}
$$

### Note:
$$
\int_a^b{dr} = b-a
$$
$$
\int_a^b{\frac{1}{r^2}dr} = -(\frac{1}{b}-\frac{1}{a})=\frac{b-a}{ab}
$$
that is
$$
\int_a^b(1\pm {\frac{a^2}{r^2}})dr =(b-a)\pm\frac{a(b-a)}{b}=(b-a)(1\pm\frac{a}{b})
$$


### Generated voltage
$$
V =-\int_a^b{(g_{31} (T_1(r)+T_2(r))+g_{33} T_3(r))dr}
$$
$$
V =-A(b-a)\big(g_{31}(2+\frac{a}{b})+g_{33}(1-\frac{a}{b})\big)
$$
$$
V =-P_b b \frac{b}{b+a}\big(g_{31}(2+\frac{a}{b})+g_{33}(1-\frac{a}{b})\big)
$$

Let $t=\frac{a}{b}$ then
$$
V =-P_b b \frac{1}{1+t}\big(g_{31}(2+t)+g_{33}(1-t)\big)
$$


## Sphere
$$
T_3(r)=P_b\frac{b^3}{a^3-b^3}(1-\frac{a^3}{r^3})
$$
$$
T_1(r)=T_2(r)=P_b\frac{b^3}{a^3-b^3}(1+\frac{a^3}{2r^3})
$$

With
$$
\int_a^b{\frac{1}{r^3}dr} = -\frac{1}{2}(\frac{1}{b^2}-\frac{1}{a^2})=\frac{1}{2}\frac{b^2-a^2}{a^2b^2}=\frac{1}{2}(\frac{b+a}{ab})(\frac{b-a}{ab})
$$
and therefore
$$
\int_a^b(1- \frac{a^3}{r^3})dr =(b-a)-\frac{a^3}{2}\frac{b+a}{ab}\frac{b-a}{ab}=(b-a)(1-\frac{a}{2b}\frac{b+a}{b})
=\frac{(b-a)}{2}(2-\frac{a}{b}-\frac{a^2}{b^2})
$$
$$
\int_a^b(1+ \frac{1}{2}\frac{a^3}{r^3})dr =(b-a)+\frac{1}{2}\frac{a^3}{2}\frac{b+a}{ab}\frac{b-a}{ab}=(b-a)(1+\frac{1}{2}\frac{a}{2b}\frac{b+a}{b})=\frac{(b-a)}{4}(4+\frac{a}{b}+\frac{a^2}{b^2})
$$
one gets
$$
V = -P_b\frac{b^3}{b^3-a^3}\frac{(b-a)}{2}\big(g_{31}(4+\frac{a}{b}+\frac{a^2}{b^2})+g_{33}(2-\frac{a}{b}-\frac{a^2}{b^2})\big)
$$
and finally
$$
V = -P_b\frac{b}{2}\frac{b^2}{b^2+ab+a^2}\big(g_{31}(4+\frac{a}{b}+\frac{a^2}{b^2})+g_{33}(2-\frac{a}{b}-\frac{a^2}{b^2})\big)
$$
which again is equivalent to
$$
V = -P_b\frac{b}{2}\frac{1}{1+t+t^2}\big(g_{31}(4+t+t^2)+g_{33}(2-t-t^2)\big)
$$
