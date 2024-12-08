function sendButtonPress() {
    fetch('/api/button', {
        method: 'POST',
    })
    .then(response => console.log('Button press sent'))
    .catch(error => console.error('Error:', error));
}