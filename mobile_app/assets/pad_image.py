from PIL import Image
import os

def pad_image(filepath, output_path, padding_ratio=0.25):
    img = Image.open(filepath).convert("RGBA")
    width, height = img.size
    
    new_w = int(width * (1 - padding_ratio * 2))
    new_h = int(height * (1 - padding_ratio * 2))
    
    img_resized = img.resize((new_w, new_h), Image.Resampling.LANCZOS)
    
    # White background for adaptive icon
    new_img = Image.new("RGBA", (width, height), (255, 255, 255, 255))
    
    offset = ((width - new_w) // 2, (height - new_h) // 2)
    new_img.paste(img_resized, offset, img_resized)
    
    new_img.save(output_path)
    print(f"Padded {filepath} and saved to {output_path}")

pad_image("logo.png", "icon.png")
pad_image("logo.png", "splash.png")
