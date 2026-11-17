"""Genera recursos/rtxjuegos.ico (ícono del programa). Requiere Pillow: pip install pillow

Solo hace falta volver a ejecutarlo si se quiere cambiar el diseño; el .ico ya queda guardado.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

GRANDE = 1024  # se dibuja grande y se reduce para que los bordes queden suaves
SALIDA = Path(__file__).resolve().parent.parent / "recursos" / "rtxjuegos.ico"


def degradado(tam, arriba, abajo):
    img = Image.new("RGBA", (tam, tam))
    d = ImageDraw.Draw(img)
    for y in range(tam):
        t = y / (tam - 1)
        color = tuple(round(a + (b - a) * t) for a, b in zip(arriba, abajo)) + (255,)
        d.line([(0, y), (tam, y)], fill=color)
    return img


def dibujar():
    s = GRANDE
    fondo = degradado(s, (118, 255, 3), (0, 120, 60))  # verde "RTX"
    mascara = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mascara).rounded_rectangle([0, 0, s - 1, s - 1], radius=int(s * 0.22), fill=255)

    icono = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    icono.paste(fondo, (0, 0), mascara)

    # mando de juegos: cuerpo + dos empuñaduras
    mando = Image.new("L", (s, s), 0)
    m = ImageDraw.Draw(mando)
    m.rounded_rectangle([s * 0.14, s * 0.30, s * 0.86, s * 0.62], radius=int(s * 0.16), fill=255)
    m.ellipse([s * 0.10, s * 0.38, s * 0.42, s * 0.80], fill=255)
    m.ellipse([s * 0.58, s * 0.38, s * 0.90, s * 0.80], fill=255)

    # cruceta y botones (se recortan del mando)
    hueco = Image.new("L", (s, s), 0)
    h = ImageDraw.Draw(hueco)
    cx, cy, brazo, grosor = s * 0.30, s * 0.49, s * 0.10, s * 0.035
    h.rounded_rectangle([cx - brazo, cy - grosor, cx + brazo, cy + grosor], radius=int(grosor * 0.5), fill=255)
    h.rounded_rectangle([cx - grosor, cy - brazo, cx + grosor, cy + brazo], radius=int(grosor * 0.5), fill=255)
    r = s * 0.042
    for bx, by in ((0.70, 0.42), (0.78, 0.50), (0.62, 0.50), (0.70, 0.58)):
        h.ellipse([s * bx - r, s * by - r, s * bx + r, s * by + r], fill=255)

    forma = Image.composite(Image.new("L", (s, s), 0), mando, hueco)

    sombra = forma.filter(ImageFilter.GaussianBlur(s * 0.02))
    capa_sombra = Image.new("RGBA", (s, s), (0, 40, 20, 0))
    capa_sombra.putalpha(sombra.point(lambda v: v * 0.55))
    icono.alpha_composite(capa_sombra, (0, int(s * 0.02)))

    blanco = Image.new("RGBA", (s, s), (20, 24, 28, 255))
    blanco.putalpha(forma)
    icono.alpha_composite(blanco)
    return icono


def main():
    SALIDA.parent.mkdir(parents=True, exist_ok=True)
    img = dibujar()
    img.save(SALIDA, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    img.resize((256, 256), Image.LANCZOS).save(SALIDA.with_suffix(".png"))
    print(f"Ícono creado: {SALIDA}")


if __name__ == "__main__":
    main()
