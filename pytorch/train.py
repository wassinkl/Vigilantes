import argparse
import collections
import torch
import data_processing.data_loaders as module_data
import model.loss as module_loss
import model.metric as module_metric
import model.model as module_arch
from parse_config import ConfigParser
from trainer import Trainer
from sklearn import metrics
from torchvision.models.resnet import resnet34,resnet50
from torchvision.models.efficientnet import efficientnet_b0
import torch
from PIL import  Image
from torchvision import transforms
from torch import manual_seed
manual_seed(17)
from torch import argmax, topk
from torch.nn import Softmax, Linear


def main(config):
    logger = config.get_logger('train')
    # setup data_processing instances
    data_loader = config.initialize('data_processing', module_data)
    valid_data_loader = data_loader.split_validation()

    # build model architecture, then print to console
    model = resnet34(pretrained=True,progress=True)
    model.fc = torch.nn.Linear(512,397)
    logger.info(model)

    # get function handles of loss and metrics
    loss = getattr(module_loss, config['loss'])
    metrics = [getattr(module_metric, met) for met in config['metrics']]

    # build optimizer, learning rate scheduler. delete every lines containing lr_scheduler for disabling scheduler
    trainable_params = filter(lambda p: p.requires_grad, model.parameters())
    optimizer = config.initialize('optimizer', torch.optim, trainable_params)
    lr_scheduler = config.initialize('lr_scheduler', torch.optim.lr_scheduler, optimizer)

    trainer = Trainer(model, loss, metrics, optimizer,
                      config=config,
                      data_loader=data_loader,
                      valid_data_loader=valid_data_loader,
                      lr_scheduler=lr_scheduler
                      )

    trainer.train()
    torch.save(model,"saved/final.pth")

    ## PRINT OUTPUT ##
    img = Image.open("00076.jpg").convert('RGB')
    #trans2 = transforms.Resize((299,299))
    #trans3 = transforms.ToTensor()
    normalize = transforms.Normalize(mean=[0.485, 0.456, 0.406],
                                     std=[0.229, 0.224, 0.225])
    #im_tense = trans2(normalize(trans3(img)))
    preprocessing = transforms.Compose([
        transforms.Resize(299),
        transforms.ToTensor(),
        normalize,
    ])
    im_tense = preprocessing(img)
    im_tense.to('cuda')
    model.to('cpu')
    torch.save(model, "saved/cpufinal.pth")
    model.eval()
    output = model(im_tense.expand(1,im_tense.shape[0],im_tense.shape[1],im_tense.shape[2]))
    print(topk(output,3))


if __name__ == '__main__':
    torch.cuda.empty_cache()
    args = argparse.ArgumentParser(description='Cars Train')
    args.add_argument('-p', '--phase', default='train', type=str,
                        help='phase (default: train)')
    args.add_argument('-c', '--config', default=None, type=str,
                        help='config file path (default: None)')
    args.add_argument('-r', '--resume', default=None, type=str,
                        help='path to latest checkpoint (default: None)')
    args.add_argument('-d', '--device', default=None, type=str,
                        help='indices of GPUs to enable (default: all)')

    # custom cli options to modify configuration from default values given in json file.
    CustomArgs = collections.namedtuple('CustomArgs', 'flags type target')
    options = [
        CustomArgs(['--lr', '--learning_rate'], type=float, target=('optimizer', 'args', 'lr')),
        CustomArgs(['--bs', '--batch_size'], type=int, target=('data_processing', 'args', 'batch_size'))
    ]
    config = ConfigParser(args, options)
    main(config)
