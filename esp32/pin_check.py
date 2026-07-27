from machine import Pin
pins = [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,21,33,34,35,36,37,38,39,40,41,42,43,44,47,48]
for p in pins:
    try:
        Pin(p)
        print(p, 'OK')
    except Exception as e:
        print(p, 'BAD', e)
