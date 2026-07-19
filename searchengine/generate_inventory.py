import os
import shutil
import random
import string
import time

def generate_sku():
    digits = "".join(random.choices(string.digits, k=10))
    return "600" + digits

def main():
    print("=== Checkers Sixty60 1GB+ Inventory Data Generator ===")
    
    data_dir = "data"
    
    # 1. Prune existing data
    if os.path.exists(data_dir):
        print(f"Pruning existing data directory '{data_dir}'...")
        shutil.rmtree(data_dir)
        
    os.makedirs(data_dir, exist_ok=True)
    print(f"Created clean directory '{data_dir}'")

    # 2. Define highly structured, realistic product categories and lines
    categories_data = {
        "fresh_produce": {
            "name": "Fresh Produce",
            "lines": [
                {
                    "name": "Apples & Pears",
                    "brands": ["Freshmark", "Simple Truth Organic", "Checkers Odd-Sized"],
                    "adjectives": ["Crisp", "Sweet", "Golden", "Red", "Class 1"],
                    "types": ["Golden Delicious Apples", "Granny Smith Apples", "Cripps Pink Apples", "Packham Pears", "Forelle Pears"],
                    "packaging": ["1.5kg Bag", "1kg Bag", "1.5kg Box", "6 Pack"],
                    "descriptions": [
                        "Sweet and crunchy, sourced directly from Ceres valley orchards.",
                        "Class 1 quality fruit, selected for excellent texture, freshness and flavor.",
                        "A natural source of energy and dietary fiber, perfect for everyday family snacks."
                    ],
                    "ingredients": "100% Fresh Apples or Pears.",
                    "allergens": "None.",
                    "is_food": True,
                    "price_min": 19.99,
                    "price_max": 49.99,
                    "nutrition_base": {"energy": 220, "protein": 0.3, "carbs": 14.0, "sugar": 10.0, "fat": 0.2, "fiber": 2.4, "sodium": 1, "calcium": 6}
                },
                {
                    "name": "Citrus & Bananas",
                    "brands": ["Freshmark", "Karsten Farms", "Stark Ayres"],
                    "adjectives": ["Sweet", "Ripe", "Juicy", "Seedless", "Organic"],
                    "types": ["Bananas", "Lemons", "Valencia Oranges", "Easy Peelers", "Grapefruit"],
                    "packaging": ["1kg Bag", "Single Item", "2kg pocket", "750g Pack"],
                    "descriptions": [
                        "Naturally sweet and packed with Vitamin C to support a healthy immune system.",
                        "Grown under the South African sun and picked at peak maturity for maximum flavor.",
                        "Perfect for fresh squeezing, lunchbox treats, or refreshing summer snacks."
                    ],
                    "ingredients": "100% Fresh Citrus or Bananas.",
                    "allergens": "None.",
                    "is_food": True,
                    "price_min": 9.99,
                    "price_max": 39.99,
                    "nutrition_base": {"energy": 350, "protein": 1.1, "carbs": 22.0, "sugar": 12.0, "fat": 0.3, "fiber": 2.6, "sodium": 1, "calcium": 15}
                },
                {
                    "name": "Berries & Grapes",
                    "brands": ["Freshmark", "Simple Truth Organic", "Dutoit"],
                    "adjectives": ["Sweet", "Plump", "Red Seedless", "White Seedless", "Organic"],
                    "types": ["Blueberries", "Strawberries", "Raspberries", "Red Grapes", "Green Grapes"],
                    "packaging": ["125g Punnet", "250g Punnet", "500g Pack", "400g Box"],
                    "descriptions": [
                        "Premium quality hand-picked berries and grapes, bursting with juicy sweetness.",
                        "Perfect addition to morning muesli, fresh fruit salads, or school lunchboxes.",
                        "Washed, graded and packed under strict temperature-controlled environments."
                    ],
                    "ingredients": "100% Fresh Berries or Grapes.",
                    "allergens": "None.",
                    "is_food": True,
                    "price_min": 24.99,
                    "price_max": 69.99,
                    "nutrition_base": {"energy": 240, "protein": 0.7, "carbs": 12.0, "sugar": 9.0, "fat": 0.3, "fiber": 2.0, "sodium": 2, "calcium": 10}
                },
                {
                    "name": "Salad Vegetables",
                    "brands": ["Freshmark", "ZZ2", "Simple Truth Organic"],
                    "adjectives": ["Fresh", "Crisp", "Baby", "English", "Organic"],
                    "types": ["Tomatoes", "English Cucumbers", "Baby Spinach", "Rosa Tomatoes", "Butter Lettuce", "Sweet Bell Peppers"],
                    "packaging": ["English Cucumber Single", "400g Pack", "200g Tub", "350g Packet", "3 Pack"],
                    "descriptions": [
                        "Crisp and refreshing salad ingredients, sourced daily from local farmers.",
                        "Thoroughly washed and selected to ensure maximum crunch and nutrition.",
                        "Perfect base for light summer salads or adding crisp crunch to burgers and wraps."
                    ],
                    "ingredients": "100% Fresh Salad Vegetables.",
                    "allergens": "None.",
                    "is_food": True,
                    "price_min": 12.99,
                    "price_max": 45.99,
                    "nutrition_base": {"energy": 80, "protein": 1.0, "carbs": 3.5, "sugar": 1.8, "fat": 0.1, "fiber": 1.5, "sodium": 12, "calcium": 25}
                },
                {
                    "name": "Cooking Vegetables",
                    "brands": ["Freshmark", "ZZ2", "Dutoit", "Checkers Odd-Sized"],
                    "adjectives": ["Fresh", "Class 1", "Buttery", "Sweet"],
                    "types": ["Onions", "Potatoes", "Sweet Potatoes", "Butternut Squash", "Baby Carrots", "Green Beans", "Broccoli Florets"],
                    "packaging": ["2kg Bag", "1kg Bag", "7kg Pocket", "500g Packet", "300g Packet"],
                    "descriptions": [
                        "High quality cooking vegetables, selected for outstanding texture and taste.",
                        "Essential ingredients for hearty stews, roasts, or nutritious side dishes.",
                        "Locally grown class 1 produce, packed fresh to retain natural nutrients."
                    ],
                    "ingredients": "100% Fresh Cooking Vegetables.",
                    "allergens": "None.",
                    "is_food": True,
                    "price_min": 14.99,
                    "price_max": 89.99,
                    "nutrition_base": {"energy": 280, "protein": 1.8, "carbs": 14.0, "sugar": 2.2, "fat": 0.1, "fiber": 2.5, "sodium": 8, "calcium": 20}
                }
            ]
        },
        "dairy_eggs": {
            "name": "Dairy, Milk & Eggs",
            "lines": [
                {
                    "name": "Milk & Cream",
                    "brands": ["Clover", "Crystal Valley", "First Choice", "Darling Dairy"],
                    "adjectives": ["Full Cream", "Low Fat", "Fat Free", "Fresh", "Long Life"],
                    "types": ["Fresh Milk", "UHT Long Life Milk", "Double Cream Milk", "Whipping Cream", "Vanilla Custard"],
                    "packaging": ["2L Bottle", "1L Carton", "6 Pack (6x1L)", "250ml Brick", "500ml Bottle"],
                    "descriptions": [
                        "Pasteurized milk from pasture-fed cows on certified local dairy farms.",
                        "Creamy, delicious taste. A rich source of calcium for healthy bones and teeth.",
                        "South African household favorite, perfect for hot beverages, cereals, or cooking."
                    ],
                    "ingredients": "Pasteurized Cow's Milk.",
                    "allergens": "Contains Cow's Milk.",
                    "is_food": True,
                    "price_min": 18.99,
                    "price_max": 119.99,
                    "nutrition_base": {"energy": 270, "protein": 3.4, "carbs": 4.8, "sugar": 4.8, "fat": 3.3, "fiber": 0.0, "sodium": 50, "calcium": 120}
                },
                {
                    "name": "Cheese Block & Slices",
                    "brands": ["Lancewood", "Crystal Valley", "Clover", "Fairview"],
                    "adjectives": ["Mature", "Mild", "Grated", "Sliced", "Traditional"],
                    "types": ["Cheddar Cheese", "Gouda Cheese", "Mozzarella Cheese", "Feta Cheese"],
                    "packaging": ["400g Block", "250g Packet", "600g Tub", "150g Tub", "800g Block"],
                    "descriptions": [
                        "Naturally aged cheese with a rich, full-bodied taste and smooth texture.",
                        "Excellent melting properties, perfect for toasted sandwiches, pizzas, and pasta dishes.",
                        "Made from select cow's milk using traditional recipes for superior quality."
                    ],
                    "ingredients": "Cow's Milk, Salt, Cheese Cultures, Rennet (Non-Animal).",
                    "allergens": "Contains Cow's Milk.",
                    "is_food": True,
                    "price_min": 32.99,
                    "price_max": 149.99,
                    "nutrition_base": {"energy": 1600, "protein": 25.0, "carbs": 1.3, "sugar": 0.1, "fat": 33.0, "fiber": 0.0, "sodium": 650, "calcium": 700}
                },
                {
                    "name": "Yogurt & Cream Cheese",
                    "brands": ["Clover", "Lancewood", "Darling Dairy", "Fairview"],
                    "adjectives": ["Double Cream", "Smooth", "Plain Creamy", "Sweet Chilli", "Low Fat"],
                    "types": ["Plain Yogurt", "Strawberry Yogurt", "Vanilla Yogurt", "Cream Cheese", "Smooth Cottage Cheese"],
                    "packaging": ["1kg Tub", "6 Pack (6x150g)", "500g Tub", "250g Tub", "150g Tub"],
                    "descriptions": [
                        "Creamy and velvety texture, loaded with live beneficial dairy cultures.",
                        "Ideal for delicious family breakfasts, active daily snacks, or baking recipes.",
                        "Naturally thick and smooth, crafted with milk from sustainable dairy farms."
                    ],
                    "ingredients": "Cow's Milk, Reconstituted Whey Powder, Sugar (if flavored), Yogurt Cultures.",
                    "allergens": "Contains Cow's Milk.",
                    "is_food": True,
                    "price_min": 19.99,
                    "price_max": 64.99,
                    "nutrition_base": {"energy": 420, "protein": 3.8, "carbs": 12.0, "sugar": 11.0, "fat": 3.5, "fiber": 0.0, "sodium": 60, "calcium": 110}
                },
                {
                    "name": "Butter & Margarine spreads",
                    "brands": ["LURPAK", "Clover", "Crystal Valley", "Flora", "Rama"],
                    "adjectives": ["Salted", "Unsalted", "Creamy", "Medium Fat", "Original"],
                    "types": ["Butter Brick", "Butter Tub", "Margarine Spread", "Fat Spread"],
                    "packaging": ["250g Brick", "500g Tub", "500g Brick", "1kg Tub"],
                    "descriptions": [
                        "Rich butter churned from fresh cream, or spreads made with heart-healthy oils.",
                        "Perfect for cooking, baking delicious pastries, or spreading on fresh bread.",
                        "Classic buttery taste that enhances the flavor of any home-cooked dish."
                    ],
                    "ingredients": "Cream (from Cow's Milk), Salt (for salted butter) or Vegetable Oils, Emulsifiers, Vitamin A & D.",
                    "allergens": "Contains Cow's Milk (butter) or Soy (margarine).",
                    "is_food": True,
                    "price_min": 24.99,
                    "price_max": 99.99,
                    "nutrition_base": {"energy": 3000, "protein": 0.5, "carbs": 0.7, "sugar": 0.1, "fat": 81.0, "fiber": 0.0, "sodium": 550, "calcium": 15}
                },
                {
                    "name": "Eggs",
                    "brands": ["Nulaid", "Checkers Housebrand", "Simple Truth Organic"],
                    "adjectives": ["Free Range", "Fresh", "Large", "Extra Large", "Jumbo"],
                    "types": ["Eggs"],
                    "packaging": ["18 Pack", "30 Pack", "6 Pack", "12 Pack"],
                    "descriptions": [
                        "Grade A quality farm-fresh eggs with clean, strong shells.",
                        "Rich and vibrant yellow yolks, perfect for frying, boiling, scrambling, or baking.",
                        "Sourced from local poultry farms committed to ethical and sustainable farming."
                    ],
                    "ingredients": "100% Chicken Eggs.",
                    "allergens": "Contains Eggs.",
                    "is_food": True,
                    "price_min": 21.99,
                    "price_max": 94.99,
                    "nutrition_base": {"energy": 600, "protein": 12.5, "carbs": 0.6, "sugar": 0.0, "fat": 10.0, "fiber": 0.0, "sodium": 140, "calcium": 50}
                }
            ]
        },
        "bakery": {
            "name": "Bakery & Bread",
            "lines": [
                {
                    "name": "Sliced Breads",
                    "brands": ["Albany", "SASKO", "Blue Ribbon", "Simple Truth"],
                    "adjectives": ["Sliced White", "Sliced Brown", "Whole Wheat", "Low GI", "Gluten-Free"],
                    "types": ["Bread", "Rye Bread"],
                    "packaging": ["700g Loaf", "600g Loaf", "800g Loaf"],
                    "descriptions": [
                        "Freshly baked daily, soft and fluffy bread fortified with essential vitamins.",
                        "High in fiber, ideal for packing school lunchboxes or making delicious toast.",
                        "Soft texture that holds up well with all your favorite spreads and sandwich fillings."
                    ],
                    "ingredients": "Wheat Flour (Gluten), Water, Yeast, Sugar, Salt, Soya Flour, Preservatives.",
                    "allergens": "Contains Wheat, Gluten, Soy.",
                    "is_food": True,
                    "price_min": 15.99,
                    "price_max": 38.99,
                    "nutrition_base": {"energy": 1000, "protein": 8.0, "carbs": 48.0, "sugar": 3.0, "fat": 1.5, "fiber": 5.0, "sodium": 400, "calcium": 80}
                },
                {
                    "name": "Bakery Rolls & Buns",
                    "brands": ["Checkers Bakery", "SASKO", "Blue Ribbon"],
                    "adjectives": ["Freshly Baked", "Buttery", "Sesame Seed", "Sweet", "Traditional"],
                    "types": ["Hot Dog Rolls", "Hamburger Buns", "Cocktail Rolls", "Chelsea Buns", "Scones"],
                    "packaging": ["6 Pack", "4 Pack", "12 Pack", "Single Item"],
                    "descriptions": [
                        "Freshly baked by our in-store bakers using quality flour and ingredients.",
                        "Soft, light, and golden-brown buns, perfect for hot dogs, hamburgers, or braais.",
                        "Indulgently sweet and soft rolls, best served warm with a spreading of butter."
                    ],
                    "ingredients": "Wheat Flour (Gluten), Water, Yeast, Sugar, Vegetable Fat, Salt, Emulsifiers.",
                    "allergens": "Contains Wheat, Gluten. May contain Sesame Seeds.",
                    "is_food": True,
                    "price_min": 12.99,
                    "price_max": 29.99,
                    "nutrition_base": {"energy": 1150, "protein": 8.5, "carbs": 52.0, "sugar": 6.0, "fat": 2.5, "fiber": 3.0, "sodium": 450, "calcium": 60}
                },
                {
                    "name": "Pastries & Sweet Treats",
                    "brands": ["Checkers Bakery", "Bakers", "Salignac"],
                    "adjectives": ["Freshly Baked", "Chocolate Chip", "Gluten-Free", "Flaky", "Buttery"],
                    "types": ["Croissants", "Muffins", "Cookies", "Swiss Roll", "Ciabatta"],
                    "packaging": ["4 Pack", "6 Pack", "200g Box", "450g Pack", "Single Item"],
                    "descriptions": [
                        "Rich, buttery, flaky pastries crafted from traditional recipes.",
                        "Baked with real chocolate chips or sweet fruit pieces for a delicious flavor burst.",
                        "A delightful sweet treat that pairs perfectly with coffee, tea, or a quiet break."
                    ],
                    "ingredients": "Wheat Flour, Butter (Milk), Water, Sugar, Yeast, Eggs, Salt, Cocoa Mass (if chocolate).",
                    "allergens": "Contains Wheat, Gluten, Eggs, Cow's Milk.",
                    "is_food": True,
                    "price_min": 19.99,
                    "price_max": 59.99,
                    "nutrition_base": {"energy": 1650, "protein": 6.0, "carbs": 45.0, "sugar": 18.0, "fat": 20.0, "fiber": 2.0, "sodium": 350, "calcium": 40}
                },
                {
                    "name": "Biscuits & Crackers",
                    "brands": ["Bakers", "Simple Truth"],
                    "adjectives": ["Original", "Sweet", "Crispy", "Toasted", "Salted"],
                    "types": ["Marie Biscuits", "Tennis Biscuits", "Cream Crackers", "Crumpets"],
                    "packaging": ["200g Packet", "150g Packet", "400g Box", "250g Packet"],
                    "descriptions": [
                        "A South African household staple. Perfect for dunking in your morning tea.",
                        "Crisp, sweet, and buttery biscuits, ideal for baking traditional tart bases.",
                        "Lightly salted, flaky cream crackers that pair excellently with cheese and spreads."
                    ],
                    "ingredients": "Wheat Flour, Sugar, Vegetable Oil (Palm Fruit), Butter, Salt, Raising Agents, Emulsifier.",
                    "allergens": "Contains Wheat, Gluten, Cow's Milk, Soy.",
                    "is_food": True,
                    "price_min": 14.99,
                    "price_max": 39.99,
                    "nutrition_base": {"energy": 1850, "protein": 6.5, "carbs": 70.0, "sugar": 22.0, "fat": 16.0, "fiber": 2.0, "sodium": 380, "calcium": 30}
                }
            ]
        },
        "meat_poultry": {
            "name": "Meat, Poultry & Seafood",
            "lines": [
                {
                    "name": "Beef & Lamb Cuts",
                    "brands": ["Checkers Championship", "Simple Truth Free Range"],
                    "adjectives": ["Lean", "Extra Lean", "Tender", "Aged", "Prime"],
                    "types": ["Beef Mince", "Rump Steak", "Lamb Rib Chops", "Beef T-Bone Steak", "Stewing Beef"],
                    "packaging": ["500g Tray", "1kg Tray", "4 Pack", "700g Pack", "800g Tray"],
                    "descriptions": [
                        "Premium quality South African red meat, wet-aged for superb tenderness.",
                        "Versatile mince or succulent steaks, ideal for home cooking or a hot braai.",
                        "Sourced from selected farms adhering to high standards of animal welfare."
                    ],
                    "ingredients": "100% Beef or Lamb.",
                    "allergens": "None.",
                    "is_food": True,
                    "price_min": 59.99,
                    "price_max": 289.99,
                    "nutrition_base": {"energy": 900, "protein": 20.0, "carbs": 0.0, "sugar": 0.0, "fat": 15.0, "fiber": 0.0, "sodium": 70, "calcium": 10}
                },
                {
                    "name": "Fresh Poultry",
                    "brands": ["County Fair", "Rainbow", "Goldi", "Simple Truth Free Range"],
                    "adjectives": ["Lean", "Fresh", "Free Range", "Tender", "Marinated"],
                    "types": ["Chicken Breast Fillets", "Chicken Drumsticks", "Whole Chicken", "Chicken Wings"],
                    "packaging": ["500g Tray", "1kg Pack", "1.2kg Pack", "800g Tray", "4 Pack"],
                    "descriptions": [
                        "Succulent fresh chicken raised on local farms. High in protein, low in fat.",
                        "Tender breast fillets or juicy drumsticks, perfect for healthy family dinners.",
                        "Ethically raised poultry with no added water or growth stimulants."
                    ],
                    "ingredients": "100% Chicken.",
                    "allergens": "None.",
                    "is_food": True,
                    "price_min": 44.99,
                    "price_max": 129.99,
                    "nutrition_base": {"energy": 650, "protein": 22.0, "carbs": 0.0, "sugar": 0.0, "fat": 7.0, "fiber": 0.0, "sodium": 80, "calcium": 15}
                },
                {
                    "name": "Wors & Sausages",
                    "brands": ["Checkers Championship", "Eskort", "Rainbow"],
                    "adjectives": ["Traditional", "Spiced", "Smoked", "Braai", "Lean"],
                    "types": ["Boerewors", "Vienna Sausages", "Pork Sausages", "Chicken Sausages"],
                    "packaging": ["1kg Pack", "500g Packet", "400g Tray", "500g Tray"],
                    "descriptions": [
                        "Award-winning boerewors spiced with traditional coriander, cloves, and vinegar.",
                        "Classic, wood-smoked viennas or pork sausages, perfect for breakfast or hot dogs.",
                        "Made from select meat cuts using authentic recipes for maximum flavor."
                    ],
                    "ingredients": "Beef, Pork, Water, Cereal (Wheat Flour, Gluten), Salt, Spices, Vinegar, Preservatives.",
                    "allergens": "Contains Wheat, Gluten.",
                    "is_food": True,
                    "price_min": 29.99,
                    "price_max": 119.99,
                    "nutrition_base": {"energy": 1200, "protein": 15.0, "carbs": 3.0, "sugar": 0.5, "fat": 25.0, "fiber": 0.5, "sodium": 750, "calcium": 25}
                },
                {
                    "name": "Bacon & Cold Meats",
                    "brands": ["Eskort", "Enterprise", "Checkers Championship"],
                    "adjectives": ["Wood-Smoked", "Honey Cured", "Thick Cut", "Streaky", "Lean"],
                    "types": ["Streaky Bacon", "Back Bacon", "French Polony", "Salami Slices", "Vienna Sausages"],
                    "packaging": ["250g Packet", "1kg Roll", "150g Packet", "400g Tray"],
                    "descriptions": [
                        "Wood-smoked pork bacon or finely sliced cold meats, cured to lock in flavor.",
                        "Essential breakfast companion, pizza topping, or sandwich filler.",
                        "High quality cured meats, prepared under strict hygiene standards."
                    ],
                    "ingredients": "Pork, Water, Salt, Sugar, Stabilizers, Antioxidant (Sodium Erythorbate), Preservatives.",
                    "allergens": "Contains Soy.",
                    "is_food": True,
                    "price_min": 22.99,
                    "price_max": 99.99,
                    "nutrition_base": {"energy": 1400, "protein": 14.0, "carbs": 1.5, "sugar": 0.5, "fat": 30.0, "fiber": 0.0, "sodium": 1100, "calcium": 10}
                },
                {
                    "name": "Dried Meats (Biltong)",
                    "brands": ["Saval", "Checkers Championship"],
                    "adjectives": ["Traditional", "Chili", "Beef", "Snap Sticks", "Salty"],
                    "types": ["Beef Biltong", "Droewors", "Chili Bites"],
                    "packaging": ["150g Packet", "250g Packet", "80g Packet", "100g Packet"],
                    "descriptions": [
                        "Traditional South African biltong, air-dried and spiced with coriander, salt, and vinegar.",
                        "High protein, low carb snack, ideal for sport events, road trips, or lunchboxes.",
                        "Perfect snack option, crafted from premium beef silverside."
                    ],
                    "ingredients": "Beef, Vinegar, Salt, Spices, Coriander, Black Pepper, Preservatives.",
                    "allergens": "None.",
                    "is_food": True,
                    "price_min": 34.99,
                    "price_max": 139.99,
                    "nutrition_base": {"energy": 1100, "protein": 45.0, "carbs": 2.0, "sugar": 1.0, "fat": 8.0, "fiber": 0.0, "sodium": 1800, "calcium": 20}
                }
            ]
        },
        "beverages": {
            "name": "Drinks & Beverages",
            "lines": [
                {
                    "name": "Soft Drinks & Mixers",
                    "brands": ["Coca-Cola", "Schweppes", "Appletiser", "Monster", "BOS"],
                    "adjectives": ["Original Taste", "Sugar Free", "Sparkling", "Zero Sugar", "Rooibos Infused"],
                    "types": ["Soft Drink", "Tonic Water", "Apple Juice", "Energy Drink", "Ice Tea"],
                    "packaging": ["2L Bottle", "330ml Can", "6 Pack Cans", "1.5L Bottle", "500ml Bottle"],
                    "descriptions": [
                        "Crisp, refreshing taste. The ultimate beverage companion for any meal or social event.",
                        "Serve chilled for a burst of bubbles and refreshing local flavor.",
                        "Available in classic sugar formulas or light, sugar-free alternatives."
                    ],
                    "ingredients": "Carbonated Water, Sugar (or Sweeteners), Caramel Color, Citric Acid, Flavorings, Caffeine.",
                    "allergens": "None.",
                    "is_food": True,
                    "price_min": 11.99,
                    "price_max": 94.99,
                    "nutrition_base": {"energy": 180, "protein": 0.0, "carbs": 10.6, "sugar": 10.6, "fat": 0.0, "fiber": 0.0, "sodium": 10, "calcium": 2}
                },
                {
                    "name": "Fruit Juices",
                    "brands": ["Clover Krush", "Liqui-Fruit", "Ceres"],
                    "adjectives": ["100% Pure", "Fresh", "Unsweetened", "Sparkling"],
                    "types": ["Apple Juice", "Orange Juice", "Mango Juice", "Tropical Juice", "Guava Juice"],
                    "packaging": ["1L Box", "2L Bottle", "330ml Can", "6 Pack Cans"],
                    "descriptions": [
                        "Made from 100% pure fruit juice with no added sugar, colorants, or artificial preservatives.",
                        "Naturally sweet and rich in Vitamin C, making it a delicious start to the morning.",
                        "Sourced from the best South African orchards for pure, refreshing fruit flavor."
                    ],
                    "ingredients": "Fruit Juice Concentrate (Apple, Orange, or Mango), Purified Water, Vitamin C, Natural Flavors.",
                    "allergens": "None.",
                    "is_food": True,
                    "price_min": 14.99,
                    "price_max": 48.99,
                    "nutrition_base": {"energy": 200, "protein": 0.4, "carbs": 11.2, "sugar": 10.2, "fat": 0.1, "fiber": 0.2, "sodium": 5, "calcium": 10}
                },
                {
                    "name": "Water",
                    "brands": ["Valpre", "Clover", "Checkers"],
                    "adjectives": ["Still", "Sparkling", "Natural", "Purified"],
                    "types": ["Mineral Water", "Soda Water"],
                    "packaging": ["1.5L Bottle", "500ml Bottle", "6 Pack (6x500ml)", "750ml Bottle"],
                    "descriptions": [
                        "Pure mineral water naturally filtered and bottled at the source.",
                        "Excellent for daily hydration, exercise, or serving at dinner parties.",
                        "Clean, crisp taste that keeps you refreshed throughout the South African heat."
                    ],
                    "ingredients": "Natural Mineral Water.",
                    "allergens": "None.",
                    "is_food": True,
                    "price_min": 6.99,
                    "price_max": 59.99,
                    "nutrition_base": {"energy": 0, "protein": 0.0, "carbs": 0.0, "sugar": 0.0, "fat": 0.0, "fiber": 0.0, "sodium": 5, "calcium": 15}
                },
                {
                    "name": "Coffee & Tea",
                    "brands": ["Jacobs", "Ricoffy", "Freshpak", "Joko", "Five Roses"],
                    "adjectives": ["Instant", "Rooibos", "Decaf", "Tagless", "Original"],
                    "types": ["Instant Coffee", "Tea Bags", "Hot Chocolate"],
                    "packaging": ["200g Jar", "250g Tin", "80 Tagless Bags", "100 Pack", "500g Bag"],
                    "descriptions": [
                        "Rich, aromatic blend that delivers a warm, comforting coffee or tea experience.",
                        "Naturally caffeine-free rooibos tea, loaded with healthy antioxidants.",
                        "A classic South African pantry essential, perfect for kickstarting your day."
                    ],
                    "ingredients": "Pure Coffee Beans or Rooibos/Black Tea Leaves (or Cocoa Powder, Sugar, Whey Powder for Hot Chocolate).",
                    "allergens": "None (tea/coffee) or Contains Cow's Milk (hot chocolate).",
                    "is_food": True,
                    "price_min": 19.99,
                    "price_max": 169.99,
                    "nutrition_base": {"energy": 10, "protein": 0.2, "carbs": 1.0, "sugar": 0.0, "fat": 0.1, "fiber": 0.5, "sodium": 2, "calcium": 10}
                }
            ]
        },
        "snacks_treats": {
            "name": "Snacks & Treats",
            "lines": [
                {
                    "name": "Potato & Corn Chips",
                    "brands": ["Simba", "Lays", "Doritos", "Willards"],
                    "adjectives": ["Fruit Chutney", "Salt & Vinegar", "Cheese & Onion", "Sweet Chilli", "Cheese Supreme"],
                    "types": ["Potato Chips", "Corn Chips"],
                    "packaging": ["120g Packet", "150g Packet", "36g Packet"],
                    "descriptions": [
                        "Roarrrs with flavor! Sliced from quality potatoes and fried to golden crispiness.",
                        "Crunchy corn chips seasoned with bold, mouth-watering spices. Perfect for dipping.",
                        "The ultimate South African snack for sharing during sport games or family get-togethers."
                    ],
                    "ingredients": "Potatoes (or Corn), Vegetable Oil, Salt, Sugar, Spices, Yeast Extract, Citric Acid.",
                    "allergens": "Contains Soy, Wheat (Gluten).",
                    "is_food": True,
                    "price_min": 7.99,
                    "price_max": 27.99,
                    "nutrition_base": {"energy": 2200, "protein": 6.5, "carbs": 50.0, "sugar": 2.5, "fat": 33.0, "fiber": 4.0, "sodium": 650, "calcium": 25}
                },
                {
                    "name": "Chocolate Bars & Slabs",
                    "brands": ["Cadbury", "Nestle", "Beacon"],
                    "adjectives": ["Dairy Milk", "Milk Chocolate", "Whole Nut", "Fruit & Nut", "Caramel"],
                    "types": ["Chocolate Bar", "Chocolate Slab"],
                    "packaging": ["80g Slab", "150g Slab", "36g Bar", "200g Box"],
                    "descriptions": [
                        "Creamy milk chocolate made with a glass and a half of fresh milk for a rich taste.",
                        "Indulgently smooth chocolate bar, filled with crunchy nuts or rich flowing caramel.",
                        "The perfect sweet treat to satisfy your chocolate cravings or gift to loved ones."
                    ],
                    "ingredients": "Sugar, Cow's Milk Solids, Cocoa Butter, Cocoa Mass, Vegetable Fats, Emulsifiers, Flavorings.",
                    "allergens": "Contains Cow's Milk, Soy. May contain Tree Nuts, Wheat (Gluten).",
                    "is_food": True,
                    "price_min": 11.99,
                    "price_max": 69.99,
                    "nutrition_base": {"energy": 2250, "protein": 7.0, "carbs": 57.0, "sugar": 55.0, "fat": 30.0, "fiber": 2.0, "sodium": 80, "calcium": 180}
                },
                {
                    "name": "Gummies & Marshmallows",
                    "brands": ["Maynards", "Beacon"],
                    "adjectives": ["Chewy", "Sour", "Wine", "Sweet", "Fluffy"],
                    "types": ["Gummy Sweets", "Marshmallows"],
                    "packaging": ["150g Packet", "75g Packet", "200g Bag", "400g Bag"],
                    "descriptions": [
                        "Chewy fruit-flavored gums made with real fruit juice. Bursting with sweet flavor.",
                        "Soft, fluffy marshmallows, perfect for braai s'mores or topping your hot chocolate.",
                        "A classic sweet snack that is loved by children and adults alike."
                    ],
                    "ingredients": "Sugar, Glucose Syrup, Water, Gelatine, Citric Acid, Pectin, Colorants, Flavorings.",
                    "allergens": "Prepared in a facility that may process Gluten, Egg, Milk.",
                    "is_food": True,
                    "price_min": 9.99,
                    "price_max": 44.99,
                    "nutrition_base": {"energy": 1450, "protein": 3.5, "carbs": 80.0, "sugar": 65.0, "fat": 0.1, "fiber": 0.0, "sodium": 40, "calcium": 5}
                },
                {
                    "name": "Nuts & Dried Fruits",
                    "brands": ["Safari", "Checkers"],
                    "adjectives": ["Roasted & Salted", "Raw", "Seedless", "Salted", "Healthy"],
                    "types": ["Peanuts & Raisins", "Cashews", "Dried Raisins", "Mixed Nuts"],
                    "packaging": ["250g Packet", "100g Packet", "500g Tub", "150g Packet"],
                    "descriptions": [
                        "A wholesome mix of roasted peanuts and sweet raisins. Great for active daily energy.",
                        "Premium quality raw or roasted nuts, high in healthy fats, protein, and dietary fiber.",
                        "Naturally sweet, sun-dried seedless raisins, perfect for baking or healthy snacking."
                    ],
                    "ingredients": "Peanuts, Raisins, Cashews, Almonds, Vegetable Oil, Salt.",
                    "allergens": "Contains Peanuts, Tree Nuts.",
                    "is_food": True,
                    "price_min": 12.99,
                    "price_max": 99.99,
                    "nutrition_base": {"energy": 1800, "protein": 15.0, "carbs": 35.0, "sugar": 28.0, "fat": 28.0, "fiber": 6.0, "sodium": 200, "calcium": 60}
                }
            ]
        },
        "pantry": {
            "name": "Pantry & Canned Goods",
            "lines": [
                {
                    "name": "Rice & Pasta Staples",
                    "brands": ["Tastic", "Fatti's & Moni's", "Checkers"],
                    "adjectives": ["Long Grain", "Parboiled", "Durum Wheat", "Macaroni"],
                    "types": ["Rice", "Spaghetti", "Macaroni"],
                    "packaging": ["2kg Bag", "500g Packet", "1kg Packet", "5kg Bag", "10kg Bag"],
                    "descriptions": [
                        "Perfect, separate, fluffy grains every single time. A South African staple.",
                        "Premium pasta made from 100% durum wheat semolina, cooking to a perfect al dente.",
                        "Versatile carbohydrate base for curries, stews, pasta bakes, or salads."
                    ],
                    "ingredients": "Long Grain Parboiled Rice or Durum Wheat Semolina (Gluten).",
                    "allergens": "None (rice) or Contains Wheat (Gluten) (pasta).",
                    "is_food": True,
                    "price_min": 15.99,
                    "price_max": 189.99,
                    "nutrition_base": {"energy": 1480, "protein": 7.5, "carbs": 78.0, "sugar": 0.5, "fat": 1.0, "fiber": 1.5, "sodium": 5, "calcium": 10}
                },
                {
                    "name": "Canned Staples",
                    "brands": ["Koo", "Lucky Star", "Checkers"],
                    "adjectives": ["Baked", "Tomato", "Hot Chili", "Original"],
                    "types": ["Baked Beans", "Pilchards in Tomato Sauce", "Chopped Tomatoes", "Tomato Paste"],
                    "packaging": ["410g Can", "400g Can", "155g Can", "400g Can"],
                    "descriptions": [
                        "Delicious baked beans in a rich tomato sauce. Packed with fiber and protein.",
                        "Rich in Omega-3 fatty acids, canned pilchards offer a convenient, healthy meal solution.",
                        "Premium quality chopped tomatoes, ideal for building rich pasta and curry sauces."
                    ],
                    "ingredients": "White Beans (or Pilchards), Water, Tomato Paste, Sugar, Salt, Modified Starch, Spices.",
                    "allergens": "Contains Fish (pilchards) or Soy (beans).",
                    "is_food": True,
                    "price_min": 9.99,
                    "price_max": 29.99,
                    "nutrition_base": {"energy": 380, "protein": 5.5, "carbs": 12.0, "sugar": 5.0, "fat": 1.5, "fiber": 4.5, "sodium": 380, "calcium": 40}
                },
                {
                    "name": "Sauces & Condiments",
                    "brands": ["All Gold", "Mrs Ball's", "Nola", "Crosse & Blackwell"],
                    "adjectives": ["Tomato", "Original Peach", "Creamy", "Tangy", "Hot"],
                    "types": ["Tomato Sauce", "Chutney", "Mayonnaise"],
                    "packaging": ["700ml Bottle", "470g Bottle", "750g Jar", "375g Squeeze"],
                    "descriptions": [
                        "Crammed full of real tomatoes. Free from thickeners, colorants, or preservatives.",
                        "Original sweet and tangy peach chutney, the essential companion for curries and braais.",
                        "Creamy, thick mayonnaise that adds a rich, tangy zip to salads and sandwiches."
                    ],
                    "ingredients": "Tomato Paste, Sugar, Vinegar, Salt, Spices (sauce) or Peaches, Raisins, Sugar, Vinegar (chutney) or Vegetable Oil, Egg Yolks, Water, Vinegar (mayo).",
                    "allergens": "None (tomato/chutney) or Contains Eggs (mayonnaise).",
                    "is_food": True,
                    "price_min": 21.99,
                    "price_max": 54.99,
                    "nutrition_base": {"energy": 550, "protein": 1.0, "carbs": 25.0, "sugar": 22.0, "fat": 4.0, "fiber": 1.0, "sodium": 600, "calcium": 12}
                },
                {
                    "name": "Baking & Cooking Oils",
                    "brands": ["Huletts", "Sunfoil", "Gold Star", "Robertsons", "Knorr"],
                    "adjectives": ["White Cane", "Brown Cane", "Pure Sunflower", "Chicken Flavor"],
                    "types": ["Sugar", "Sunflower Oil", "Stock Cubes", "Salt Shaker"],
                    "packaging": ["2.5kg Bag", "2L Bottle", "750ml Bottle", "10 Pack", "500g Bag"],
                    "descriptions": [
                        "Pure cane sugar, finely milled to dissolve quickly. Perfect for hot drinks and baking.",
                        "Triple-refined pure sunflower oil, excellent for shallow frying and baking.",
                        "Flavorful stock cubes and seasonings to add rich depth to your stews and soups."
                    ],
                    "ingredients": "Cane Sugar (sugar) or Pure Sunflower Oil (oil) or Salt, Flavor Enhancers, Vegetable Fat, Spices (stock).",
                    "allergens": "None (sugar/oil) or Contains Gluten, Soy (stock).",
                    "is_food": True,
                    "price_min": 14.99,
                    "price_max": 99.99,
                    "nutrition_base": {"energy": 1600, "protein": 0.0, "carbs": 99.9, "sugar": 99.9, "fat": 0.0, "fiber": 0.0, "sodium": 5, "calcium": 2}
                }
            ]
        },
        "frozen_foods": {
            "name": "Frozen Foods",
            "lines": [
                {
                    "name": "Frozen Veg & Chips",
                    "brands": ["McCain", "Harvestime"],
                    "adjectives": ["Crispy", "Oven Chips", "Mixed", "Garden"],
                    "types": ["Potato Chips", "Mixed Vegetables", "Garden Peas"],
                    "packaging": ["1kg Bag", "2kg Bag", "500g Packet"],
                    "descriptions": [
                        "Crispy on the outside, soft and fluffy inside. Ready to bake or fry.",
                        "Flash-frozen shortly after harvesting to lock in natural sweetness, color, and vitamins.",
                        "Convenient and nutritious vegetable options, ready to steam or boil in minutes."
                    ],
                    "ingredients": "Potatoes, Vegetable Oil (chips) or Peas, Corn, Carrots (mixed vegetables).",
                    "allergens": "None.",
                    "is_food": True,
                    "price_min": 22.99,
                    "price_max": 79.99,
                    "nutrition_base": {"energy": 520, "protein": 2.5, "carbs": 18.0, "sugar": 1.5, "fat": 3.5, "fiber": 3.0, "sodium": 45, "calcium": 22}
                },
                {
                    "name": "Frozen Fish & Seafood",
                    "brands": ["I&J", "Sea Harvest"],
                    "adjectives": ["Crispy Batter", "Crumbed", "Wild-Caught"],
                    "types": ["Fish Fingers", "Hake Fillets"],
                    "packaging": ["400g Box", "800g Box", "600g Bag"],
                    "descriptions": [
                        "Coated in a light, crispy batter. A quick, delicious dinner option kids love.",
                        "Premium quality wild-caught white fish fillets, high in protein and low in fat.",
                        "Sourced sustainably from clean oceans and frozen at sea to lock in freshness."
                    ],
                    "ingredients": "White Fish (Hake), Wheat Flour (Gluten), Water, Sunflower Oil, Starch, Salt, Yeast, Spices.",
                    "allergens": "Contains Fish, Wheat (Gluten).",
                    "is_food": True,
                    "price_min": 39.99,
                    "price_max": 149.99,
                    "nutrition_base": {"energy": 780, "protein": 13.0, "carbs": 15.0, "sugar": 1.0, "fat": 8.0, "fiber": 1.2, "sodium": 420, "calcium": 20}
                },
                {
                    "name": "Frozen Pizzas & Pies",
                    "brands": ["Checkers Bella Vita", "Dr. Oetker", "Today"],
                    "adjectives": ["Woodfired", "Deep Pan", "Regina", "Sausage"],
                    "types": ["Regina Pizza", "Pepperoni Pizza", "Sausage Rolls", "Puff Pastry"],
                    "packaging": ["Single Pizza", "400g Box", "300g Box", "500g Packet"],
                    "descriptions": [
                        "Woodfired pizza base loaded with rich tomato sauce and premium mozzarella cheese.",
                        "Golden flaky puff pastry rolls filled with seasoned savory meat.",
                        "Convenient ready-rolled pastry sheets, perfect for baking sweet or savory pies."
                    ],
                    "ingredients": "Wheat Flour (Gluten), Water, Mozzarella Cheese (Milk), Tomato Puree, Ham, Mushrooms, Vegetable Oils, Salt, Yeast, Herbs.",
                    "allergens": "Contains Wheat, Gluten, Cow's Milk. May contain Soy.",
                    "is_food": True,
                    "price_min": 29.99,
                    "price_max": 89.99,
                    "nutrition_base": {"energy": 1050, "protein": 10.5, "carbs": 26.0, "sugar": 2.5, "fat": 11.0, "fiber": 2.0, "sodium": 580, "calcium": 180}
                },
                {
                    "name": "Frozen Desserts",
                    "brands": ["Ola", "Magnum", "Checkers Housebrand"],
                    "adjectives": ["Rich Vanilla", "Double Chocolate", "Creamy", "Chocolate Shell"],
                    "types": ["Ice Cream Tub", "Magnum Bar Multipack"],
                    "packaging": ["1.5L Tub", "4 Pack", "2L Tub"],
                    "descriptions": [
                        "Thick, cracking Belgian chocolate shell surrounding velvety smooth vanilla ice cream.",
                        "Velvety, creamy ice cream tub, the perfect dessert to share with the whole family.",
                        "Made with high quality dairy ingredients for a rich, comforting dessert experience."
                    ],
                    "ingredients": "Water, Dairy Solids (Milk), Sugar, Vegetable Fat, Cocoa Butter, Cocoa Mass, Emulsifiers, Stabilizers, Flavorings.",
                    "allergens": "Contains Cow's Milk, Soy. May contain Tree Nuts.",
                    "is_food": True,
                    "price_min": 39.99,
                    "price_max": 109.99,
                    "nutrition_base": {"energy": 1100, "protein": 3.0, "carbs": 24.0, "sugar": 22.0, "fat": 15.0, "fiber": 0.5, "sodium": 60, "calcium": 100}
                }
            ]
        },
        "household": {
            "name": "Household & Cleaning",
            "lines": [
                {
                    "name": "Toilet Paper & Tissues",
                    "brands": ["Baby Soft", "Twinsaver"],
                    "adjectives": ["Double Velvet", "2-Ply", "Soft & Strong", "White"],
                    "types": ["Toilet Paper", "Facial Tissues"],
                    "packaging": ["9 Rolls", "18 Rolls", "150 Pack", "200 Pack"],
                    "descriptions": [
                        "Luxuriously soft and strong toilet tissue, dermatologically approved for skin safety.",
                        "Made from 100% virgin pulp fibers for premium absorption and hygiene.",
                        "Perfect for everyday family hygiene, providing gentle care and strength."
                    ],
                    "ingredients": "100% Virgin Cellulose Pulp.",
                    "allergens": "Dermatologically tested. Hypoallergenic.",
                    "is_food": False,
                    "price_min": 29.99,
                    "price_max": 169.99
                },
                {
                    "name": "Household Cleaners & Bleach",
                    "brands": ["Sunlight", "Handy Andy", "Domestos", "Harpic"],
                    "adjectives": ["Lemon Fresh", "Multi-purpose", "Thick Lavender", "Power Active"],
                    "types": ["Dishwashing Liquid", "Cleaning Cream", "Bleach", "Toilet Cleaner"],
                    "packaging": ["750ml Bottle", "500ml Spray", "750ml Squeeze", "1.5L Bottle"],
                    "descriptions": [
                        "Grease-cutting dishwashing liquid with a refreshing lemon scent, gentle on hands.",
                        "Abrasive multi-purpose cream cleaner that easily lifts stubborn dirt, grease, and grime.",
                        "Thick bleach sanitizer that kills 99.9% of all known germs and provides deep hygiene."
                    ],
                    "ingredients": "Anionic Surfactants, Non-ionic Surfactants, Sodium Hypochlorite (for bleach), Colorants, Perfume.",
                    "allergens": "Keep out of reach of children. Avoid contact with eyes and skin.",
                    "is_food": False,
                    "price_min": 18.99,
                    "price_max": 59.99
                },
                {
                    "name": "Laundry Detergents & Softeners",
                    "brands": ["Omo", "Sunlight", "Comfort", "Vanish"],
                    "adjectives": ["Auto Concentrated", "Handwash Fresh", "Lavender Softener", "Stain Remover"],
                    "types": ["Washing Powder", "Washing Liquid", "Fabric Softener", "Stain Powder"],
                    "packaging": ["2kg Box", "2L Bottle", "800ml Bottle", "1kg Bag"],
                    "descriptions": [
                        "Penetrates deep into fibers to remove tough stains in a single wash. Perfect for machines.",
                        "Enriches and softens fabric fibers, reducing static and leaving clothes smelling fresh.",
                        "Powerful stain remover that is safe on colors and tough on ground-in dirt."
                    ],
                    "ingredients": "Surfactants, Sodium Carbonate, Enzymes, Optical Brighteners, Fabric Softeners, Fragrance.",
                    "allergens": "Skin irritant. Avoid prolonged contact. Keep away from children.",
                    "is_food": False,
                    "price_min": 34.99,
                    "price_max": 129.99
                }
            ]
        },
        "baby": {
            "name": "Baby Care",
            "lines": [
                {
                    "name": "Baby Nappies & Wipes",
                    "brands": ["Pampers", "Huggies"],
                    "adjectives": ["Baby-Dry", "Sensitive Wipes", "Gold", "Active Fit"],
                    "types": ["Nappies Size 4", "Baby Wipes", "Nappies Size 3"],
                    "packaging": ["Value Pack (50 Pack)", "64 Pack", "Jumbo Pack (66 Pack)"],
                    "descriptions": [
                        "Features an ultra-absorbent core that locks in moisture for up to 12 hours of dry comfort.",
                        "Gently cleanses baby's delicate skin. Alcohol-free, fragrance-free, and dermatologically tested.",
                        "Designed with stretchy sides and soft materials for premium fit and leakage protection."
                    ],
                    "ingredients": "Polyacrylate Absorbent, Polypropylene Fiber, Cellulose Pulp, Aloe Vera extracts (wipes).",
                    "allergens": "Dermatologically tested. Hypoallergenic.",
                    "is_food": False,
                    "price_min": 39.99,
                    "price_max": 249.99
                },
                {
                    "name": "Baby Food & Cereal",
                    "brands": ["Purity", "Nestle Cerelac"],
                    "adjectives": ["Organic", "Nutritious", "Smooth", "Maize"],
                    "types": ["Pear Baby Food Pouch", "Maize Baby Cereal", "Butternut Puree Jar"],
                    "packaging": ["110ml Pouch", "250g Box", "125g Jar"],
                    "descriptions": [
                        "Smooth fruit puree made from 100% natural fruit with no added sugar or starch.",
                        "Easy-to-digest infant cereal fortified with iron and essential vitamins for growth.",
                        "Prepared under strict quality guidelines to ensure nutritious, safe meals for infants."
                    ],
                    "ingredients": "Pears (pouch) or Maize Flour, Skimmed Milk Powder, Sugar, Vegetable Oil, Minerals, Vitamins.",
                    "allergens": "None (fruit pouch) or Contains Cow's Milk, Gluten (cereal).",
                    "is_food": True,
                    "price_min": 11.99,
                    "price_max": 59.99,
                    "nutrition_base": {"energy": 320, "protein": 2.0, "carbs": 15.0, "sugar": 8.0, "fat": 0.5, "fiber": 2.0, "sodium": 15, "calcium": 60}
                },
                {
                    "name": "Baby Toiletries",
                    "brands": ["Johnson's", "Elizabeth Anne's"],
                    "adjectives": ["Gentle", "Bedtime", "Mild", "Soothing"],
                    "types": ["Baby Shampoo", "Baby Lotion", "Baby Powder", "Teething Gel"],
                    "packaging": ["200ml Bottle", "500ml Bottle", "200g Tub", "100g Tube"],
                    "descriptions": [
                        "Soap-free, mild formula that is gentle on baby's eyes and scalp. No more tears.",
                        "Deeply moisturizes and protects baby's sensitive skin, soothing before bedtime.",
                        "Clinically proven mildness designed to minimize risk of skin irritation."
                    ],
                    "ingredients": "Water, Coco-Glucoside, Glycerin, Sodium Benzoate, Fragrance, Mineral Oil.",
                    "allergens": "Dermatologically tested. Clinically proven mild.",
                    "is_food": False,
                    "price_min": 19.99,
                    "price_max": 84.99
                }
            ]
        }
    }

    # Review Generation Components
    reviewers = [
        "Sipho", "Sarah", "Jacob", "Thabo", "Lindiwe", "Johan", "Emily", "David", "Naledi", "Peter", 
        "Jane", "Mpho", "Chloe", "Andile", "Liezel", "Kgomotso", "Bruce", "Michael", "Zama", "Lerato", 
        "Pieter", "Anrich", "Fatima", "Muhammad", "Ayesha", "Ravi", "Devi", "Pranesh", "Naresh", "Sunil",
        "Tshepo", "Rethabile", "Busi", "Nonhlanhla", "Dumisani", "Gert", "Koos", "Sannie", "Kobus"
    ]
    
    review_titles_pos = [
        "Absolutely wonderful product", "Highly recommended!", "Great value for money", "Best in South Africa",
        "Excellent quality, fresh and good", "Very fresh and tasty!", "Super fast delivery on Sixty60", 
        "My whole family loves this", "Top class quality product", "Perfect purchase, highly satisfied", 
        "Will definitely buy this again", "Outstanding quality product, highly recommend"
    ]
    review_titles_neg = [
        "Disappointing purchase", "Could be much better", "Average quality, nothing special", 
        "Not worth the price tag", "Okay but not great", "A bit disappointing", "Ordinary standard, basic quality",
        "It was acceptable but barely", "Not what I expected from the brand", "Slight issues with packaging"
    ]

    reviews_pos_pool = [
        "I bought this last week and it has been great. The quality is exactly what I expected. Sourced locally and works/tastes very fresh.",
        "Highly recommend this product! It is fresh, rich, and works perfectly. The packaging was neat and clean upon Sixty60 delivery.",
        "This is a staple in my household. Checkers Sixty60 delivered it cold, fast, and in perfect condition. Unbeatable convenience.",
        "Excellent quality and highly affordable. You can taste/see the premium difference immediately. Five stars all the way!",
        "Great product, great price. Sourced locally in South Africa which I absolutely love. Will definitely order this again on Sixty60.",
        "My kids absolutely love this. It is healthy, delicious, and convenient. Always on our weekly Sixty60 shopping list.",
        "Top notch quality. Truly lives up to the brand reputation. Highly satisfied with this purchase. Outstanding service.",
        "Excellent texture and flavor. It is worth every cent and makes a big difference in our household meals. Highly recommended.",
        "Perfect for daily use. Fresh, reliable, and packaged beautifully. I've ordered this multiple times and never had an issue.",
        "A wonderful local option. The price is very reasonable compared to imports, and the quality is far superior. Sourced sustainably."
    ]

    reviews_neg_pool = [
        "It is okay but could be improved. The packaging was slightly damaged on arrival, though the product inside was fine.",
        "Average quality, nothing special. It does what it is supposed to, but there are better alternatives available at Checkers.",
        "Not worth the premium price in my opinion. It works okay, but I expected better quality from this particular brand.",
        "A bit disappointing. It was slightly near its expiry date, so I had to use it quickly. Taste/performance is just average.",
        "Ordinary standard. Nothing to write home about. It is decent for daily use, but not outstanding or worth five stars.",
        "Average purchase. It met basic expectations but did not blow me away. Packaging could be more sturdy to prevent issues.",
        "It is acceptable, but the quality has gone down compared to previous purchases. Hope the supplier improves it.",
        "Decent value, but the taste/texture was slightly off this time. Might try a different brand next week.",
        "Simple and functional, but definitely lacks the premium feel of competitors. Good if you are on a tight budget.",
        "Nothing majorly wrong, but it just does not stand out. Sizing is also a bit smaller than expected."
    ]

    total_files = 100000
    categories = list(categories_data.keys())
    
    start_time = time.time()
    print(f"Generating {total_files} files inside '{data_dir}'...")

    for i in range(total_files):
        # Evenly distribute products across categories
        cat_key = categories[i % len(categories)]
        cat = categories_data[cat_key]
        
        # Select product line deterministically based on index to ensure even distribution
        lines = cat["lines"]
        line = lines[(i // len(categories)) % len(lines)]
        
        brand = random.choice(line["brands"])
        adj = random.choice(line["adjectives"])
        p_type = random.choice(line["types"])
        packaging = random.choice(line["packaging"])
        
        desc1 = random.choice(line["descriptions"])
        desc2 = random.choice(line["descriptions"])
        while desc2 == desc1 and len(line["descriptions"]) > 1:
            desc2 = random.choice(line["descriptions"])
            
        product_title = f"{brand} {adj} {p_type} {packaging}"
        sku = generate_sku()
        
        # Generate ZAR price inside line constraints
        price_min = line.get("price_min", 15.00)
        price_max = line.get("price_max", 99.00)
        price = round(random.uniform(price_min, price_max), 2)
            
        stock = random.randint(0, 500)
        rating = round(random.uniform(3.5, 5.0), 1)
        
        is_food = line.get("is_food", False)
        
        # Generate detailed ingredients / specifications
        details_content = (
            f"Ingredients / Composition: {line['ingredients']} Sourced sustainably and packaged under strict quality control. "
            f"Storage / Care Instructions: Keep in a cool, dry place. For fresh items, store in refrigerator at or below 4°C. "
            f"Environmental Commitment: Packed in 100% recyclable materials, supporting South African eco-initiatives."
        )
        
        if is_food:
            nut_base = line["nutrition_base"]
            # Add slight variance to nutrition base
            def vary(val):
                if isinstance(val, float):
                    return round(val * random.uniform(0.9, 1.1), 1)
                elif isinstance(val, int):
                    return int(val * random.uniform(0.9, 1.1))
                return val
            
            specs_table = (
                f"Nutritional Information per 100g serving:\n"
                f"  Energy (kJ): {vary(nut_base['energy'])} kJ\n"
                f"  Protein (g): {vary(nut_base['protein'])} g\n"
                f"  Total Carbohydrates (g): {vary(nut_base['carbs'])} g\n"
                f"    of which Total Sugar (g): {vary(nut_base['sugar'])} g\n"
                f"  Total Fat (g): {vary(nut_base['fat'])} g\n"
                f"  Dietary Fiber (g): {vary(nut_base['fiber'])} g\n"
                f"  Sodium (mg): {vary(nut_base['sodium'])} mg\n"
                f"  Calcium (mg): {vary(nut_base['calcium'])} mg"
            )
            
            usage_directions = (
                f"Preparation and Serving Instructions: Ready to eat or cook according to preference. Serve chilled or hot "
                f"as a refreshing daily snack or incorporated into home recipes. For hot dishes, heat thoroughly in a pan, microwave, "
                f"or oven until steaming hot. Pair with other Checkers Sixty60 essentials for a perfect South African meal."
            )
        else:
            specs_table = (
                f"Product Specifications and Dimensions:\n"
                f"  Net Weight/Volume: {packaging}\n"
                f"  Packaging Dimensions: {random.randint(10, 50)}cm x {random.randint(10, 50)}cm x {random.randint(5, 30)}cm\n"
                f"  Gross Weight (g): {random.randint(100, 3000)} g\n"
                f"  Material Type: {adj} Recyclable Fiber / Plastic Blend\n"
                f"  Dermatologically Approved: Yes\n"
                f"  Hypoallergenic Formula: Yes\n"
                f"  SABS Grade A certified: Yes\n"
                f"  Safety Class: Class A\n"
                f"  Barcode (EAN): 600{random.randint(1000000000, 9999999999)}"
            )
            
            usage_directions = (
                f"Directions for Application and Safety: Apply to the target area or surface. Use only as directed. "
                f"For laundry or household cleaners, add the recommended dose directly to the machine dispenser or water bucket. "
                f"Ensure the container lid is closed tightly after every use to preserve freshness. Store in a cool, dry place "
                f"away from direct South African sunlight and heat sources."
            )

        # Extended Marketing Copy (Adds ~1500 chars of high quality context)
        marketing1 = (
            f"Welcome to the premium selection of {brand}. For generations, {brand} has been a trusted household name in "
            f"South African homes, bringing warmth, reliability, and superior quality to families. This {adj} {p_type} "
            f"has been manufactured under strict hygiene and control protocols, ensuring that it delivers exceptional performance "
            f"from the first use to the last. Each batch undergoes rigorous testing before leaving the facility."
        )
        
        marketing2 = (
            f"At Checkers, we believe in supporting our local South African economy. This product is proudly sourced and packed "
            f"locally, helping to create jobs and support community projects across South Africa. When you choose {brand}, "
            f"you are making a sustainable choice that benefits our local farms and workers, while enjoying products formulated "
            f"specifically to suit South African conditions and customer preferences."
        )
        
        marketing3 = (
            f"Whether you are planning a weekend braai with friends, keeping your home sparkling clean, packing your child's lunch, "
            f"or indulging in a quiet tea time treat, this {p_type} is designed to make your life simpler and more enjoyable. "
            f"Experience the ultimate convenience by ordering your daily essentials on Sixty60. We deliver directly to your door "
            f"in 60 minutes, ensuring your items arrive fresh, safe, and cold."
        )
        
        marketing4 = (
            f"Environmental responsibility is at the heart of the {brand} philosophy. We have reduced our carbon footprint by "
            f"sourcing ingredients locally, using energy-efficient production methods, and moving to fully recyclable paper and "
            f"plastic packaging. Enjoy peace of mind knowing that your purchase contributes to a cleaner, greener South Africa "
            f"for future generations."
        )
        
        extended_marketing = f"{marketing1}\n\n{marketing2}\n\n{marketing3}\n\n{marketing4}"

        # Generate 16 detailed customer reviews (Adds ~6500 chars)
        reviews_list = []
        for r_idx in range(16):
            reviewer = random.choice(reviewers)
            r_rating = random.choice([4, 5]) if rating >= 4.3 else random.choice([3, 4, 5])
            review_title = random.choice(review_titles_pos) if r_rating >= 4 else random.choice(review_titles_neg)
            
            comment1 = random.choice(reviews_pos_pool) if r_rating >= 4 else random.choice(reviews_neg_pool)
            comment2 = random.choice(reviews_pos_pool) if r_rating >= 4 else random.choice(reviews_neg_pool)
            comment3 = f"I will recommend {brand} {p_type} to my friends and relatives." if r_rating >= 4 else "I hope Checkers fixes this soon."
            
            day = random.randint(1, 28)
            month = random.randint(1, 12)
            date_str = f"2026-{month:02d}-{day:02d}"
            
            review_text = (
                f"Review #{r_idx + 1}:\n"
                f"  Reviewer Name: {reviewer}\n"
                f"  Submission Date: {date_str}\n"
                f"  Customer Rating: {r_rating} out of 5 stars\n"
                f"  Review Title: {review_title}\n"
                f"  Detailed Comment: {comment1} {comment2} {comment3}\n"
            )
            reviews_list.append(review_text)
            
        reviews_block = "\n".join(reviews_list)
        
        # Assemble product details into a rich document (~11,000 bytes)
        content = (
            f"--- START Sixty60 PRODUCT RECORD ---\n"
            f"ProductTitle: {product_title}.\n"
            f"Brand: {brand}.\n"
            f"Category: {cat['name']} > {line['name']}.\n"
            f"SKU: Sixty60-{sku}.\n"
            f"Price: R {price} ZAR.\n"
            f"Stock Status: {stock} units in stock.\n"
            f"Average Customer Rating: {rating} stars.\n"
            f"WeightVolume: {packaging}.\n"
            f"General Description: {adj} {p_type} from {brand}. {desc1} {desc2} Freshness and quality guaranteed.\n\n"
            f"--- CORE PRODUCT INFORMATION ---\n"
            f"{details_content}\n\n"
            f"--- DETAILED SPECIFICATIONS AND PROPERTIES ---\n"
            f"{specs_table}\n\n"
            f"--- APPLICATION AND USAGE DIRECTIVES ---\n"
            f"{usage_directions}\n\n"
            f"--- BRAND HERITAGE AND SUSTAINABILITY ---\n"
            f"{extended_marketing}\n\n"
            f"--- CUSTOMER SATISFACTION REVIEWS AND SENTIMENTS ---\n"
            f"{reviews_block}\n"
            f"--- END Sixty60 PRODUCT RECORD ---\n"
        )
        
        # File name: e.g., data/pantry-6001018023456.txt
        filename = f"{cat_key}-{sku}.txt"
        filepath = os.path.join(data_dir, filename)
        
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(content)
            
        if (i + 1) % 10000 == 0:
            elapsed = time.time() - start_time
            print(f"Generated {i + 1}/{total_files} files... ({elapsed:.2f}s elapsed)")

    total_time = time.time() - start_time
    print(f"\nSuccessfully generated {total_files} files in {total_time:.2f} seconds!")
    print(f"Checkers Sixty60 data files are available at '{data_dir}/'")

if __name__ == "__main__":
    main()
