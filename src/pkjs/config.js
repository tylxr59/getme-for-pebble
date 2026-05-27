module.exports = [
  {
    type: 'heading',
    defaultValue: 'getme for Pebble'
  },
  {
    type: 'section',
    items: [
      {
        type: 'input',
        messageKey: 'getmeUrl',
        label: 'GetMe URL',
        description: 'Full URL to your getme install.',
        attributes: {
          type: 'text',
          placeholder: 'https://example.com/getme/'
        }
      }
    ]
  },
  {
    type: 'submit',
    defaultValue: 'Save'
  }
];
