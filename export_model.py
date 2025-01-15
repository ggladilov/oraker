from ultralytics import YOLO

YOLO('best.pt').export(format='onnx', dynamic=False, half=False)
