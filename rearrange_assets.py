import pathlib
import typing
import re

def is_image(path : pathlib.Path):
    image_matcher = re.compile(r'[0-9]+.png')
    return path.is_file() and image_matcher.match(path.name)

def is_annotation(path : pathlib.Path):
    annotation_matcher = re.compile(r'[0-9]+.json')
    return path.is_file() and annotation_matcher.match(path.name)

def rearrange(root : pathlib.Path, filter : typing.Callable, max_size : int = 150):
    version_dir_matcher = re.compile(r'^ver([0-9]+)$')

    def get_version_index(version : pathlib.Path):
        return int(re.search(version_dir_matcher, version.name).group(1))

    def get_next(version : pathlib.Path):
        version_number = get_version_index(version)
        return pathlib.Path(version.parent / re.sub(r'[0-9]+', str(version_number + 1), version.name))

    def get_file_index(file : pathlib.Path):
        return int(file.stem)

    versions = []
    for path in root.iterdir():
        if path.is_dir() and version_dir_matcher.match(path.name):
            versions.append(path)
    versions = sorted(versions, key=get_version_index)

    for idx, version in enumerate(versions):
        if get_version_index(version) == 0:
            # skip ver0 as exception
            continue

        files = sorted([file for file in version.iterdir() if filter(file)], key=get_file_index)
        if len(files) <= max_size:
            continue

        if idx + 1 < len(versions):
            destination = versions[idx + 1]
        else:
            destination = get_next(version)
            destination.mkdir(parents=True)
            versions.append(destination)

        for to_move in files[max_size:]:
            to_move.rename(destination / to_move.name)

rearrange(pathlib.Path(r'./assets/images'), is_image)
rearrange(pathlib.Path(r'./assets/annotations'), is_annotation)
