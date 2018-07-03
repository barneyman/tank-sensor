import pip

def install(package):
    pip.main(['install', package])

# Example
if __name__ == '__main__':
    install('flask')
    install('upgrade google-api-python-client')
    install('oauth2client')

