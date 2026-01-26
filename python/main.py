import sys
import os

sys.path.append(os.path.dirname(__file__))

from fastapi import FastAPI
import mycppmodule

print(mycppmodule.multiply(2, 5))

app = FastAPI()

@app.get("/")
def read_root():
    return {"message": "Welcome to PriceRiot API"}

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("main:app", host="127.0.0.1", port=8000, reload=True)
