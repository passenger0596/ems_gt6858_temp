from asgiref.wsgi import WsgiToAsgi
from app import app

# 将 Flask WSGI 应用包装成 ASGI 应用
asgi_app = WsgiToAsgi(app)
