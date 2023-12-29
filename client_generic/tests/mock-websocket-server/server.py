import asyncio
import http.server
import socketserver
import socket
import threading
from starlette.applications import Starlette
from starlette.websockets import WebSocketDisconnect
import json
import logging
import uvicorn

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

app = Starlette()

websockets = {
    'web': {},
    'desktop': {},
}

async def receive_json(websocket):
    message = await websocket.receive_text()
    return json.loads(message)

@app.websocket_route('/ws')
async def websocket_endpoint(websocket):
    await websocket.accept()

    # "Authentication" message
    message = await receive_json(websocket)
    client_mode = message['client_mode']
    client_id = message['client_id']
    websockets[client_mode][client_id] = websocket

    # Get mirror mode to broadcast messages to the client on the other side
    mirror_mode = 'web' if client_mode == 'desktop' else 'desktop'

    client_string = f'{client_id}[{client_mode}]'
    logger.info(f'Client connected: {client_string}')

    while (True):
        try:
            # Wait for a message from the client
            message = await receive_json(websocket)
            logger.debug(f'Message received from {client_string}: {message}')

            try:
                # Broadcast it to the mirror client
                await websockets[mirror_mode][client_id].send_text(
                    json.dumps(message)
                )
            except KeyError:
                logger.debug(
                    f'Client {client_id}[{mirror_mode}] not connected'
                )
        except WebSocketDisconnect:
            break

    del websockets[client_mode][client_id]
    await websocket.close()
    logger.info(f'Client disconnected: {client_string}')

def run_servers():
    # Set the ports you want to use
    http_port = 8000
    websocket_port = 9000

    # Get local IP address
    temp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    temp_socket.connect(("8.8.8.8", 80))
    local_ip = temp_socket.getsockname()[0]
    temp_socket.close()

    # Start the HTTP server
    http_handler = http.server.SimpleHTTPRequestHandler
    httpd = socketserver.TCPServer(("", http_port), http_handler)
    print(f"HTTP server running at http://{local_ip}:{http_port}")

    # Start the WebSocket server
    uvicorn_config = {"host": "0.0.0.0", "port": websocket_port}
    print(f"WebSocket server running at http://{local_ip}:{websocket_port}")
     # Run the HTTP server in a separate thread
    http_server_thread = threading.Thread(target=httpd.serve_forever)
    http_server_thread.start()

    # Run the WebSocket server using uvicorn
    uvicorn.run(app, host="0.0.0.0", port=websocket_port)

    # Wait for the HTTP server thread to finish
    http_server_thread.join()

if __name__ == '__main__':
    run_servers()
